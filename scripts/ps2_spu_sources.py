"""Prepare the pinned Play! DSP component without its emulator save states."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from project import ROOT, reject_link

STATE_METHODS = (
    'CSpuBase::LoadState', 'CSpuBase::SaveState',
    'CSpuIrqWatcher::LoadState', 'CSpuIrqWatcher::SaveState',
    'CSpuBase::CSampleReader::LoadState', 'CSpuBase::CSampleReader::SaveState',
)


def strip_state_methods(text):
    for name in STATE_METHODS:
        pattern = rf'^void {re.escape(name)}\([^\n]*\)(?: const)?\n\{{\n.*?^\}}\n'
        text, count = re.subn(pattern, '', text, flags=re.M | re.S)
        if count != 1:
            raise ValueError(f'Expected one pinned state method: {name}')
    return text


def cache_recent_samples(name, text):
    """Keep a direct lookup for hot ADPCM blocks. The multimap owns every entry."""
    if name == 'Iop_SpuBase.h':
        changes = [
            ('#include <map>', '#include <array>\n#include <map>'),
            ('const ITEM* GetItem(const KEY&) const;', '''CSpuSampleCache() = default;
		CSpuSampleCache(const CSpuSampleCache&) = delete;
		CSpuSampleCache& operator=(const CSpuSampleCache&) = delete;
		const ITEM* GetItem(const KEY&) const;'''),
            ('std::multimap<uint32, ITEM> m_cache;', '''std::multimap<uint32, ITEM> m_cache;
		static constexpr unsigned RECENT_COUNT = 2048;
		mutable std::array<const decltype(m_cache)::value_type*, RECENT_COUNT> m_recent{};
		static unsigned RecentIndex(uint32 address) { return (address / 16) % RECENT_COUNT; }'''),
        ]
    else:
        changes = [
            ('auto range = m_cache.equal_range(key.address);', '''auto& recent = m_recent[RecentIndex(key.address)];
	if(recent && recent->first == key.address &&
	   recent->second.inS1 == key.s1 && recent->second.inS2 == key.s2)
		return &recent->second;
	auto range = m_cache.equal_range(key.address);'''),
            ('\t\t\treturn &item;', '\t\t\trecent = &*itemIterator;\n\t\t\treturn &item;'),
            ('auto& item = result->second;', 'm_recent[RecentIndex(key.address)] = &*result;\n\tauto& item = result->second;'),
            ('m_cache.clear();', 'm_recent.fill(nullptr);\n\tm_cache.clear();'),
            ('m_cache.erase(lowerBound, upperBound);', '''for(auto it = lowerBound; it != upperBound; ++it)
		m_recent[RecentIndex(it->first)] = nullptr;
	m_cache.erase(lowerBound, upperBound);'''),
        ]
    for original, replacement in changes:
        if text.count(original) != 1:
            raise ValueError(f'Pinned sample cache differs in {name}: {original}')
        text = text.replace(original, replacement)
    return text


def adapt(name, text):
    if name in ('Iop_SpuBase.h', 'Iop_SpuBase.cpp'):
        text = cache_recent_samples(name, text)
    if name == 'Iop_SpuBase.cpp':
        text = strip_state_methods(text)
        for header in ('RegisterStateCollectionFile', 'RegisterStateUtils', 'RegisterStateFile'):
            text = text.replace(f'#include "../states/{header}.h"\n', '')
        start = text.index('void CSpuBase::Render(')
        end = text.index('\nuint32 CSpuBase::GetAdsrDelta', start)
        render = text[start:end]
        # Scale the streamed PCM and ADPCM voices before saturation. The guest
        # volume registers, envelopes and reverb routing keep their own values.
        for original, replacement in (
            ('unsigned int sampleCount)', 'unsigned int sampleCount, int32 streamGain, int32 voiceGain)'),
            ('\t\t\tif(inputSample == 0) continue;',
             '\t\t\tif(voiceGain != 0x8000) inputSample = (inputSample * voiceGain) / 0x8000;\n'
             '\t\t\tif(inputSample == 0) continue;'),
            ('\t\t\tm_blockReader.GetSamples(blockSamples);',
             '\t\t\tm_blockReader.GetSamples(blockSamples);\n'
             '\t\t\tif(streamGain != 0x8000)\n\t\t\t{\n'
             '\t\t\t\tblockSamples[0] = (blockSamples[0] * streamGain) / 0x8000;\n'
             '\t\t\t\tblockSamples[1] = (blockSamples[1] * streamGain) / 0x8000;\n\t\t\t}'),
        ):
            if render.count(original) != 1:
                raise ValueError('Pinned DSP volume hook differs')
            render = render.replace(original, replacement)
        text = text[:start] + render + text[end:]
    elif name == 'Iop_SpuBase.h':
        text = text.replace('#include "zip/ZipArchiveWriter.h"\n', '')
        text = text.replace('#include "zip/ZipArchiveReader.h"\n', '')
        text = text.replace('#include <map>\n', '#include <map>\n#include <vector>\n#include <memory>\n')
        text = re.sub(r'^\s*void (?:LoadState|SaveState)\([^\n]*\)(?: const)?;\n', '', text, flags=re.M)
        declaration = 'void Render(int16*, unsigned int);'
        if text.count(declaration) != 1:
            raise ValueError('Pinned DSP render declaration differs')
        text = text.replace(declaration,
            'void Render(int16*, unsigned int, int32 streamGain = 0x8000, int32 voiceGain = 0x8000);')
    elif name == 'Log.h':
        # Retain the upstream no-op logging branch; no frontend/file-system
        # dependency or logging singleton is needed by this release DSP target.
        start = text.index('#if LOGGING_ENABLED\n')
        end = text.index('#else\n', start)
        text = text[end + len('#else\n'):]
        text = text[:text.rindex('#endif')]
        text = '#pragma once\n' + text
    return text


def static_channels(cpp,header):
    """Collapse only voices whose per-sample work has no observable effect.

    A stopped voice with pitch still advances the decoder and IRQ state, and
    a volume sweep still advances its envelope. Both retain the original loop.
    Dense and sparse paths preserve ascending voice order and sample-major mix.
    """
    declaration='\t\t\tint32 GetSample();\n'
    if header.count(declaration)!=1: raise ValueError('Pinned sample-reader declaration differs')
    header=header.replace(declaration,declaration+'\t\t\tbool HasStaticCursor() const;\n')
    reader='int32 CSpuBase::CSampleReader::GetSample()\n'
    if cpp.count(reader)!=1: raise ValueError('Pinned sample-reader definition differs')
    cpp=cpp.replace(reader,'''bool CSpuBase::CSampleReader::HasStaticCursor() const
{
    return m_sampleStep == 0 && m_srcSampleIdx < BUFFER_SAMPLES * PITCH_BASE &&
           !m_done && !m_didChangeRepeat;
}

'''+reader)
    start=cpp.index('void CSpuBase::Render(')
    end=cpp.index('\nuint32 CSpuBase::GetAdsrDelta',start)
    render=cpp[start:end]
    volume_update = ('\t\t\tchannel.volumeLeftAbs = ComputeChannelVolume(channel.volumeLeft, channel.volumeLeftAbs);\n'
                     '\t\t\tchannel.volumeRightAbs = ComputeChannelVolume(channel.volumeRight, channel.volumeRightAbs);\n')
    if render.count(volume_update) != 1:
        raise ValueError('Pinned per-voice volume update differs')
    render = render.replace(volume_update, '')
    anchor='\tmemset(samples, 0, sizeof(int16) * sampleCount);\n'
    if render.count(anchor)!=1: raise ValueError('Pinned DSP render initializer differs')
    render=render.replace(anchor,anchor+'''
    unsigned activeChannels[24], activeCount = 0;
    CHANNEL* sweepingChannels[24];
    unsigned sweepCount = 0;
    const auto updateVolume = [this](CHANNEL& channel) {
        channel.volumeLeftAbs = ComputeChannelVolume(channel.volumeLeft, channel.volumeLeftAbs);
        channel.volumeRightAbs = ComputeChannelVolume(channel.volumeRight, channel.volumeRightAbs);
    };
    for(unsigned i = 0; i < 24; ++i) {
        auto& channel = m_channel[i];
        auto& reader = m_reader[i];
        const bool sweeps = channel.volumeLeft.mode.mode || channel.volumeRight.mode.mode;
        if(sweeps) sweepingChannels[sweepCount++] = &channel;
        else if(ticks) updateVolume(channel);
        if(ticks && channel.status == STOPPED && channel.adsrVolume == 0 &&
           !sweeps && reader.HasStaticCursor()) {
            reader.SetRepeat(channel.repeat);
            channel.current = reader.GetCurrent();
        } else activeChannels[activeCount++] = i;
    }
''')
    loop='\t\tfor(unsigned int i = 0; i < 24; i++)\n\t\t{'
    if render.count(loop)!=1: raise ValueError('Pinned DSP channel loop differs')
    first=render.index('\tfor(unsigned int j = 0; j < ticks; j++)')
    last=render.index('\n\tif(irqEnabled && m_irqWatcher',first)
    dense=render[first:last]
    sample_start = '\t\tint16 reverbSample[2] = {};\n'
    if dense.count(sample_start) != 1:
        raise ValueError('Pinned per-sample mixer loop differs')
    dense = dense.replace(sample_start, sample_start + '''
        // Register writes occur between render calls. Only sweeps change gain within this block.
        for(unsigned i = 0; i < sweepCount; ++i) updateVolume(*sweepingChannels[i]);
''')
    sparse=dense.replace(loop,'\t\tfor(unsigned active = 0; active < activeCount; ++active)\n\t\t{\n            const unsigned i = activeChannels[active];')
    render=render[:first]+'\tif(activeCount == 24)\n\t{\n'+dense+'\t}\n\telse\n\t{\n'+sparse+'\t}\n'+render[last:]
    return cpp[:start]+render+cpp[end:],header
def prepare(dependencies, output, inventory=None, static_channel_optimization=True):
    dependencies, output = Path(dependencies), Path(output)
    inventory = inventory or ROOT / 'third_party/ps2/spu/sources.json'
    reject_link(output)
    entries = json.loads(Path(inventory).read_text(encoding='utf-8'))['files']
    staged = []
    for entry in entries:
        source, target = dependencies / entry['source'], output / entry['target']
        if '..' in Path(entry['source']).parts or '..' in Path(entry['target']).parts:
            raise ValueError('SPU inventory paths must stay within their roots')
        reject_link(source)
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError(f'Pinned SPU input differs: {source}')
        if source.suffix in ('.cpp', '.h'):
            data = adapt(source.name, data.decode('utf-8').replace('\r\n', '\n')).encode('utf-8')
        staged.append((target, data))
    if static_channel_optimization:
        by_name={target.name:(target,data) for target,data in staged}
        cpp,header=static_channels(by_name['Iop_SpuBase.cpp'][1].decode('utf8'),
                                  by_name['Iop_SpuBase.h'][1].decode('utf8'))
        optimized={'Iop_SpuBase.cpp':cpp.encode('utf8'),'Iop_SpuBase.h':header.encode('utf8')}
        staged=[(target,optimized.get(target.name,data)) for target,data in staged]
    # Validate every source before producing any adapted output.
    for target, data in staged:
        reject_link(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    return {'files': len(staged), 'removedStateMethods': list(STATE_METHODS)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dependencies', type=Path, default=ROOT / '.deps/ps2')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.dependencies, args.output)))
