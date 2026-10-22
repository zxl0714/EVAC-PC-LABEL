#include "latency/handoff.h"
#include "latency/low_handoff.h"
#include "latency/render_metrics.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <stdexcept>

using namespace sdoj::latency;
namespace {
unsigned checks{};
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
struct Fixture {
    Handoff model;
    NativeState native{100, 0x885F1A70, 0x88051350, 20, 12, 1,
        0xffffffff, 0x10000, 0x20000, 0x10100, 0x10, 0};
    explicit Fixture(unsigned mode=2) { Check(model.Load(mode), "load"); }
    void Ready(unsigned depth=1) {
        model.Logic(native); model.Commands(native.finished, native.serial);
        Check(model.Park(native, native.calls, depth, true), "park after actual baseline capture");
    }
    Publication Publish() {
        const auto publication = model.BeginPublication(native);
        Check(bool(publication), "begin publication");
        native.serial++; native.writer = native.published; native.published = native.finished;
        Check(model.Commit(*publication, native, 1), "complete native swap accepted");
        return *publication;
    }
};
}
void RunChecks() {
    for(const RenderMetrics::Pair final : {RenderMetrics::Pair{0,0},RenderMetrics::Pair{1688688,0}}) {
        // Real training transition 101048: parser produced 1688688,0;
        // native 88046514/18 reset them before the wrapper stored 0,0.
        // Neither parser-time values nor a newer batch may become the sample.
        RenderMetrics metrics;
        metrics.Complete(4843,0x100,5045,1);metrics.Tick(13441,5046);
        metrics.BeginRead(0x100);const auto token=metrics.EndRead();
        Check(token==4843 && metrics.EndRead()==0,"parser identity is taken exactly once");
        Check(!metrics.Sampled(token) && !metrics.OriginalInputs(),"intermediate parser values are not final operands");
        Check(metrics.CanResolve(token) && !metrics.CanResolve(4842) && !metrics.CanResolve(0),
              "only the exact pending parsed source can resolve before the deferred wake");
        Check(!metrics.DeferWake(13441,5046),"cannot freeze unfinalized operands");
        metrics.Complete(4844,0x200,5046,1);
        Check(metrics.Capture(token,final),"final original stores bind to the pinned batch despite producer progress");
        Check(metrics.Sampled(token) && !metrics.Sampled(4844),"finalization never retargets the newer completion");
        Check(metrics.OriginalInputs()->source==token && metrics.OriginalInputs()->values==final,
              "native training reset and unchanged normal operands both retain actual final values");
        Check(metrics.DeferWake(13441,5046),"freeze only after native finalization");
        metrics.Tick(13442,5046);
        Check(metrics.OriginalInputs()->values==final && metrics.OriginalInputs()->source==token,
              "next early slot receives the final operands, not parser-time values");
        Check(metrics.Capture(0,{999,888}) && metrics.OriginalInputs()->values==final,
              "no-parse wrapper cannot overwrite a parsed batch");
        metrics.Complete(token+8,0x100,5054,1);
        Check(!metrics.Capture(token,final),"ring reuse rejects stale finalization rather than binding by buffer");
    }
    for(unsigned depth : {1u,2u,3u,16u}) {
        Fixture f(1);LowHandoff low;low.Load(f.model.generation());
        DrawRecovery recovery;RenderMetrics metrics;
        f.native.countdown=depth;f.Ready(depth);
        const auto ready=*f.model.parked();
        metrics.Complete(ready.logic,ready.buffer,ready.baseline,ready.depth);
        // No early request or premature wake during native slowdown ticks.
        for(unsigned tick=1;tick<depth;++tick) {
            Check(!recovery.Arm(ready,f.native),"recovery cannot shorten a native slowdown wait");
            ++f.native.frame;++f.native.calls;--f.native.countdown;
        }
        metrics.Tick(f.native.frame,f.native.calls);
        Check(recovery.Arm(ready,f.native),"arm only the completed batch due for normal drawing");
        f.Publish();
        metrics.BeginRead(ready.buffer);const auto parsed=metrics.EndRead();
        Check(parsed==ready.logic && !metrics.Sampled(parsed),"parser pins identity without finalizing presentation operands");
        const auto frozen=*metrics.OriginalInputs();
        Check(recovery.Defer(f.model.parked(),f.native,parsed==ready.logic,f.model.HasReceipt()),
              "parsed immutable result permits moving its one pending wake");
        Check(!recovery.Defer(f.model.parked(),f.native,true,true),"late wake deferral is single-use");
        Check(metrics.Capture(parsed,{123,456}),"native stores finalize the exact parsed batch after late wake is deferred");
        Check(metrics.DeferWake(f.native.frame,f.native.calls),"deferred wake retains its exact slowdown input");
        Check(!metrics.DeferWake(f.native.frame,f.native.calls),"cannot defer an already deferred wake twice");
        // Even if the following early slot is paused, no invented counter or
        // zero metrics may replace the original pending wake's selection.
        for(unsigned pause=0;pause<3;++pause) {
            ++f.native.frame;metrics.Tick(f.native.frame,f.native.calls);
            const auto selected=metrics.OriginalInputs();
            Check(selected && selected->values==frozen.values && selected->source==frozen.source &&
                  selected->held==frozen.held,"same pending native wake survives host pause frames");
        }
        const auto request=low.Begin(ready,f.native);
        Check(request && request->wakes_writer && low.PrefixDone(*request),"recovery actually re-enters early scheduling");
        Check(f.model.Consume(f.native,f.native.callback),"early callback settles the deferred full swap exactly once");
        ++f.native.calls;Check(low.DriveDone(*request,f.native,1),"one native counter tick, not two");
        // The callback really runs once, then logic produces a distinct batch.
        f.model.Unpark();f.native.finished=f.native.writer;f.native.writer=0xffffffff;
        f.native.cursor=f.native.finished+0x100;f.native.countdown=1;f.Ready();
        Check(low.IsComplete(*request,f.model.parked()),"new completion belongs to the resumed early request");
        metrics.Complete(f.model.logic(),f.native.finished,f.native.calls,1);
        f.Publish();metrics.BeginRead(f.native.published);Check(metrics.Capture({789,321}),"resumed result is also parsed, not discarded");
        metrics.Tick(f.native.frame,f.native.calls);
        Check(metrics.OriginalInputs()->values==frozen.values,"same-frame parsing cannot retarget resumed worker metrics");
        Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.ConsumeDriver(f.native.frame,f.native.calls),
              "normal late hooks skip only the already executed prefix and driver");
        Check(f.model.logic()==2 && low.CanRequest(),"exactly one new update and next early request remains available");
        ++f.native.frame;metrics.Tick(f.native.frame,f.native.calls);
        Check(metrics.OriginalInputs()->source==ready.logic && metrics.OriginalInputs()->values==RenderMetrics::Pair{123,456},
              "next original native tick receives the formerly unseen real sample");
        Check(metrics.Sampled(f.model.logic()),"next parked batch was drawn so recovery does not loop at 2V");
    }
    for(unsigned mismatch=0;mismatch<12;++mismatch) {
        Fixture f(1);f.Ready();DrawRecovery recovery;
        Check(recovery.Arm(*f.model.parked(),f.native),"arm recovery identity test");f.Publish();
        auto ready=f.model.parked();auto native=f.native;bool sampled=true,receipt=true;
        switch(mismatch) {
        case 0: ++ready->generation;break;
        case 1: ++ready->logic;break;
        case 2: ++native.frame;break;
        case 3: ++native.calls;break;
        case 4: ++native.serial;break;
        case 5: native.published=0;break;
        case 6: native.writer=ready->buffer;break;
        case 7: native.countdown=0;break;
        case 8: native.renderer=0;break;
        case 9: sampled=false;break;
        case 10: receipt=false;break;
        case 11: native.busy=1;break;
        }
        Check(!recovery.Defer(ready,native,sampled,receipt),"changed identity or unparsed/unpublished result cannot suppress a wake");
        Check(!recovery.Defer(f.model.parked(),f.native,true,true),"failed recovery attempt cannot suppress a later call");
    }
    {
        RenderMetrics metrics;
        metrics.Complete(1,0x100,0xfffffffe,1);metrics.BeginRead(0x100);metrics.Capture({77,88});
        metrics.Tick(10,0xffffffff);
        Check(!metrics.DeferWake(11,0xffffffff) && !metrics.DeferWake(10,0),"wrong frame or counter cannot retain metrics");
        Check(metrics.DeferWake(10,0xffffffff),"nonzero original selection retained before counter wrap");
        metrics.Tick(11,0xffffffff);
        Check(metrics.OriginalInputs()->source==1 && metrics.OriginalInputs()->values==RenderMetrics::Pair{77,88},
              "deferred real metrics never become a synthetic held zero");
        // If the next early slot has insufficient budget, the ordinary late
        // driver still uses this selection and advances once; no special wake.
        metrics.Tick(12,0);Check(metrics.OriginalInputs()->held,"native counter wrap settles deferred selection once");
        metrics.Reset();Check(!metrics.DeferWake(0,0),"reload cannot defer stale metrics");
    }
    {
        // Runtime 101808: 19625 was completed but never parsed; 19626 was
        // already sampled. A valid older selection alone cannot authorize
        // bootstrap + early wake, which could overwrite the unseen batch.
        RenderMetrics metrics;
        metrics.Complete(19624,0x200,19956,1);
        metrics.BeginRead(0x200);Check(metrics.Capture({497584,0}),"older batch parsed");
        metrics.Complete(19625,0x100,19957,1);
        metrics.Tick(26480,19957);
        Check(metrics.OriginalInputs() && metrics.OriginalInputs()->source==19624,
              "old slowdown selection remains valid while a newer completed batch is unparsed");
        Check(!metrics.Sampled(19625),"unparsed parked batch forbids early wake despite valid old selection");
        Check(metrics.Sampled(19624),"sample identity is per completion, not per current selection");
        // Normal drawing must get its turn before the late driver wakes the
        // producer. Only then may the following boundary use the low path.
        metrics.BeginRead(0x100);Check(metrics.Capture({496000,0}),"deferred completed batch gets its real parse");
        Check(metrics.Sampled(19625),"real parse permits later early advancement");
        metrics.Complete(19626,0x200,19958,1);
        Check(!metrics.Sampled(19626),"reused buffer cannot inherit the old batch's sample");
        metrics.Tick(26481,19958);
        auto selected=metrics.OriginalInputs();
        Check(selected && selected->source==19625 && selected->values==RenderMetrics::Pair{496000,0},
              "native slowdown now consumes the previously skipped identity, not the newer batch");
        metrics.BeginRead(0x200);Check(metrics.Capture({498608,32768}),"next completion parsed separately");
        metrics.Tick(26482,19959);
        Check(metrics.OriginalInputs()->source==19626,"following frame consumes its own real sample");
        Check(!metrics.Sampled(0) && !metrics.Sampled(19618),"absent and ring-alias identities stay unknown");
        metrics.Reset();Check(!metrics.Sampled(19626),"reload drops parse permission");
    }
    {
        RenderMetrics metrics;
        metrics.Complete(1,0x100,9,1);metrics.BeginRead(0x100);metrics.Capture({7,8});
        metrics.Complete(2,0x200,10,1);
        metrics.Tick(100,11);
        Check(!metrics.OriginalInputs(),"late parser regression starts with an exact pending source");
        metrics.BeginRead(0x100);Check(metrics.Capture({7,8}),"older real parse remains consistent");
        Check(!metrics.OriginalInputs() && metrics.Inspect().missing==2,
              "a different batch must not satisfy the pending original source");
        metrics.BeginRead(0x200);Check(metrics.Capture({123,456}),"actual pending source is parsed later this frame");
        auto selected=metrics.OriginalInputs();
        Check(selected && selected->source==2 && selected->values==RenderMetrics::Pair{123,456},
              "late real parse resolves the frozen source before the original late wake");
        Check(!metrics.Inspect().missing,"resolved diagnostics no longer report a missing batch");
        metrics.Complete(3,0x100,11,1);metrics.BeginRead(0x100);metrics.Capture({789,321});
        metrics.Tick(100,12);
        Check(metrics.OriginalInputs()->source==2,"later parsing cannot retarget this video frame");
        metrics.Tick(101,12);
        Check(metrics.OriginalInputs()->source==3,"next video frame uses its own eligible batch");
        metrics.Tick(102,12);
        Check(metrics.OriginalInputs()->held,"resolved pending source is not consumed twice");
    }
    {
        RenderMetrics metrics;
        Check(!metrics.OriginalInputs(),"missing metrics never become guessed zeros");
        metrics.Complete(1,0x100,10,1);metrics.BeginRead(0x100);
        Check(metrics.Capture({123,456}),"parsed metrics captured for a complete batch");
        Check(metrics.Capture({0,0}),"held wrapper does not overwrite the parsed batch");
        metrics.Complete(2,0x200,11,1);
        metrics.Tick(100,11);
        auto selected=metrics.OriginalInputs();
        Check(selected && selected->source==1 && selected->values==RenderMetrics::Pair{123,456},
              "normal native slowdown uses the result published before this video tick");
        metrics.Tick(100,12);
        Check(metrics.OriginalInputs()->source==1,"early and late hooks cannot model one video tick twice");
        metrics.Tick(101,12);
        Check(!metrics.OriginalInputs(),"due but unparsed result remains unknown, not zero");
        const auto missing=metrics.Inspect();
        Check(missing.missing==2 && missing.consumed==1 && missing.ticked &&
              missing.frame==101 && missing.calls==12 && !missing.entries[2].sampled &&
              missing.entries[2].buffer==0x200 && missing.entries[2].due==11,
              "fault diagnostics retain the actual unknown source and scheduling counters");
        metrics.BeginRead(0x200);Check(metrics.Capture({111,222}),"second parsed batch");
        selected=metrics.OriginalInputs();
        Check(selected && selected->source==2 && selected->values==RenderMetrics::Pair{111,222},
              "a late parse resolves this video's original pending source");
        metrics.Complete(3,0x100,13,2);
        metrics.Tick(102,13);
        selected=metrics.OriginalInputs();
        Check(selected && selected->held,"the late-parsed source was already offered to the previous native late wake");
        metrics.BeginRead(0x100);Check(metrics.Capture({789,321}),"buffer reuse binds newest completed logic");
        metrics.Tick(103,14);
        selected=metrics.OriginalInputs();
        Check(selected && selected->held && selected->values==RenderMetrics::Pair{0,0},
              "original held tick supplies known zero to the unchanged decay formula");
        metrics.Tick(104,15);
        selected=metrics.OriginalInputs();
        Check(selected && selected->source==3 && selected->values==RenderMetrics::Pair{789,321},
              "new logical identity keeps its own metrics");
        metrics.Tick(105,15);
        Check(metrics.OriginalInputs()->held,"pause does not repeatedly parse the same native publication");
        metrics.BeginRead(0x100);
        Check(!metrics.Capture({790,321}),"conflicting parsed samples cannot silently replace history");
        metrics.Reset();Check(!metrics.OriginalInputs(),"module reload discards old metrics identities");
        Check(!metrics.Inspect().ticked && !metrics.Inspect().missing && !metrics.Inspect().consumed,
              "module reload cannot report stale fault counters");
        metrics.Complete(1,0x100,0xfffffffe,2);metrics.BeginRead(0x100);metrics.Capture({9,8});
        metrics.Tick(1,0xffffffff);Check(metrics.OriginalInputs()->held,"same-counter publication remains after the draw at counter wrap");
        metrics.Tick(2,0);Check(metrics.OriginalInputs()->source==1,"wrapped native publication becomes visible exactly once");
    }
    {
        Fixture f(1);LowHandoff low;low.Load(f.model.generation());
        f.native.countdown=0;
        f.model.Logic(f.native);
        Check(f.model.ParkIntermediate(f.native,f.native.calls,1),"capture an unfinished native resource update");
        for(unsigned retry=0;retry<2;++retry) {
            const auto r=low.BeginIntermediate(*f.model.intermediate(),f.native);
            Check(r && r->resumes_intermediate && r->wakes_writer,"early tick resumes the same unfinished update");
            Check(!low.IsIntermediate(*r,f.model.intermediate()),"old wait cannot satisfy its own resume request");
            Check(low.PrefixDone(*r),"resource resume uses one native input prefix");
            ++f.native.calls;Check(low.DriveDone(*r,f.native,1),"resource resume advances one native tick");
            f.model.Unpark();
            if(!retry) {
                Check(f.model.ParkIntermediate(f.native,f.native.calls,1),"resource may yield again inside this same update");
                Check(low.IsIntermediate(*r,f.model.intermediate()),"the new baseline distinguishes the next resource wait");
            } else {
                f.model.Commands(f.native.finished,f.native.serial);
                Check(f.model.Park(f.native,f.native.calls,1,true),"resource update finally completes before the draw");
                Check(low.IsComplete(*r,f.model.parked()),"same-logic resource completion is accepted without a second update");
                auto wrong=f.model.parked();++wrong->logic;
                Check(!low.IsComplete(*r,wrong),"a different logic update cannot stand in for a resource resume");
            }
            Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.ConsumeDriver(f.native.frame,f.native.calls),
                  "resource resumes keep exact-once late receipts");
            ++f.native.frame;
        }
        Check(f.model.logic()==1,"resource retries never add a logic update");
    }
    for(unsigned depth=2;depth<=16;++depth) {
        Fixture f(1);f.native.countdown=depth;f.Ready(depth);
        Check(f.model.Eligible(f.native)==Gate::Countdown,"bootstrap retains balanced timing for a slow result");
        const auto publication=f.model.BeginPublication(f.native,true);
        Check(bool(publication),"fresh low result may be shown before its multi-tick wait ends");
        ++f.native.serial;f.native.writer=f.native.published;f.native.published=f.native.finished;
        Check(f.model.Commit(*publication,f.native,1),"complete native swap for slow result");
        for(unsigned tick=1;tick<depth;++tick) {
            Check(!f.model.Consume(f.native,f.native.callback),"early receipt cannot settle on an intermediate tick");
            ++f.native.calls;--f.native.countdown;
        }
        Check(f.model.Consume(f.native,f.native.callback),"slow result settles exactly at its original callback");
        Fixture balanced;balanced.native.countdown=depth;balanced.Ready(depth);
        Check(!balanced.model.BeginPublication(balanced.native,true),"balanced cannot opt into low's deep publication");
    }
    for (bool timeout : {false,true}) {
        Fixture f(1); LowHandoff low; low.Load(f.model.generation()); f.Ready(); f.Publish();
        const auto request=low.Begin(*f.model.parked(),f.native);
        Check(request && low.PrefixDone(*request),"resource transition starts one early request");
        Check(f.model.Consume(f.native,f.native.callback),"resource request settles bootstrap swap");
        ++f.native.calls;
        Check(low.DriveDone(*request,f.native,1),"resource request owns exactly one wake");
        if(timeout)low.Timeout(*request);
        f.model.Unpark(); f.model.Logic(f.native);
        Check(f.model.ParkIntermediate(f.native,f.native.calls,1),"native resource wait before commands is observable");
        Check(low.IsIntermediate(*request,f.model.intermediate()),"same request recognizes its internal yield");
        Check(!low.IsComplete(*request,f.model.parked()) && !f.model.BeginPublication(f.native),
              "resource yield is never a completed or publishable draw buffer");
        auto stale=f.model.intermediate(); ++stale->generation;
        Check(!low.IsIntermediate(*request,stale),"foreign generation cannot resume a draining request");
        stale=f.model.intermediate(); ++stale->baseline;
        Check(!low.IsIntermediate(*request,stale),"foreign native wake cannot resume a draining request");
        if(timeout)Check(low.RecoverIntermediate(f.model.intermediate()),"late resource yield drains without needing a finished frame");
        Check(!low.SuppressWriter(),"native ticks can advance the existing partial update");
        Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.ConsumeDriver(f.native.frame,f.native.calls),
              "resource recovery retains exact-once late input and wake receipts");
        Check(!f.model.parked(),"no new low request can begin during the partial update");
        ++f.native.calls; f.model.Unpark();
        Check(!f.model.intermediate(),"native return invalidates resource wait identity");
        f.native.finished=f.native.writer; f.native.cursor=f.native.finished+0x100;
        f.native.writer=0xffffffff;
        f.model.Commands(f.native.finished,f.native.serial);
        Check(f.model.Park(f.native,f.native.calls,1,true),"same logic update eventually completes its commands");
        Check(f.model.logic()==2,"resource ticks do not create or skip a logic update");
    }
    {
        Check(WaitBudget(100,0)==0 && WaitBudget(100,99)==0 && WaitBudget(100,4100)==0,
              "no budget from invalid/expired clock or renderer reserve");
        Check(WaitBudget(100,5000)==900 && WaitBudget(100,20000)==2000,
              "borrow only remaining deadline slack up to two milliseconds");
        Fixture f(1); LowHandoff low; low.Load(f.model.generation()); f.Ready();
        f.Publish();
        Check(f.model.parked().has_value(), "native parked token survives publication");
        const auto request=low.Begin(*f.model.parked(),f.native,900);
        Check(request && request->wakes_writer && request->input_epoch==900, "request has exact parked writer and frozen input identity");
        Check(!low.Begin(*f.model.parked(),f.native), "only one early request");
        Check(low.PrefixDone(*request) && !low.PrefixDone(*request), "input conversion runs once");
        Check(f.model.Consume(f.native,f.native.callback), "early callback settles prior full swap");
        ++f.native.calls;
        Check(low.DriveDone(*request,f.native,1) && !low.DriveDone(*request,f.native,1), "callback and counter run once");
        Check(!low.IsComplete(*request,f.model.parked()), "old parked result cannot satisfy new request");
        f.model.Unpark(); f.native.finished=0x20000; f.native.cursor=0x20100; f.native.writer=0xffffffff;
        f.Ready();
        Check(low.IsComplete(*request,f.model.parked()), "new logic and baseline acknowledge this request");
        auto stale=f.model.parked(); ++stale->generation;
        Check(!low.IsComplete(*request,stale), "new module/address reuse is not request completion");
        Check(!low.ConsumePrefix(f.native.frame+1,f.native.renderer), "wrong input frame cannot skip prefix");
        Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && !low.ConsumePrefix(f.native.frame,f.native.renderer), "late native input prefix skipped once");
        Check(low.ConsumeDriver(f.native.frame,f.native.calls) && !low.ConsumeDriver(f.native.frame,f.native.calls), "late callback/counter skipped once, peripherals untouched");
        Check(low.CanRequest(), "next request only after all moved steps settle");
    }
    {
        Fixture f(1); LowHandoff low; low.Load(f.model.generation()); f.Ready(3);
        f.native.countdown=3;
        const auto r=low.Begin(*f.model.parked(),f.native);
        Check(r && !r->wakes_writer, "slowdown middle frame advances native count but does not force logic");
        Check(low.PrefixDone(*r), "slowdown input step"); ++f.native.calls;
        Check(low.DriveDone(*r,f.native,1), "slowdown countdown/wake step");
        low.Retire();
        Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.ConsumeDriver(f.native.frame,f.native.calls),
              "stop still settles moved input and driver receipts");
        Check(!low.CanRequest(), "stop prevents new requests");
    }
    {
        Fixture f(1); LowHandoff low; low.Load(f.model.generation()); f.Ready();
        Check(!low.Begin(*f.model.parked(),f.native), "due wake cannot bypass a rejected bootstrap publication");
        f.Publish();
        const auto r=low.Begin(*f.model.parked(),f.native); Check(low.PrefixDone(*r), "begin timed work");
        ++f.native.calls; Check(low.DriveDone(*r,f.native,1), "wake timed work");
        low.Timeout(*r);
        Check(low.SuppressWriter() && !low.CanRequest(), "timeout drains instead of issuing another update");
        Check(!low.Recover(f.model.parked()), "old completion does not drain the request");
        Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.ConsumeDriver(f.native.frame,f.native.calls),
              "timed-out frame still settles already executed steps");
        f.model.Unpark();
        f.native.finished=f.native.writer; f.native.cursor=f.native.finished+0x100;
        f.native.writer=0xffffffff; f.Ready();
        Check(low.Recover(f.model.parked()) && !low.SuppressWriter(), "late completion drains at next boundary");
        low.Disable(); Check(!low.CanRequest(), "latched downgrade does not automatically oscillate back");
    }
    {
        Fixture f(1); LowHandoff low; low.Load(f.model.generation()); f.Ready();
        f.Publish();
        auto r=low.Begin(*f.model.parked(),f.native);
        low.CancelBeforeDrive(*r); Check(low.CanRequest(), "pre-wake cancellation permits untouched original path");
        r=low.Begin(*f.model.parked(),f.native); low.PrefixDone(*r); low.Retire(); ++f.native.calls;
        Check(low.DriveDone(*r,f.native,1), "stop during guest call still records completed native steps");
        Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.ConsumeDriver(f.native.frame,f.native.calls),
              "stop during call cannot duplicate the work later");
    }
    {
        Fixture f(1); LowHandoff low; low.Load(f.model.generation()); f.Ready(); f.Publish();
        const auto r=low.Begin(*f.model.parked(),f.native);
        Check(low.PrefixDone(*r), "capture input before a callback declines the wake");
        low.CancelBeforeDrive(*r);
        Check(!low.CanRequest() && low.HasPrefix() && !low.HasDriver(),
              "cancelled wake retains the already executed input step only");
        Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.CanRequest(),
              "cancelled wake settles its input before another request");
    }
    for (unsigned depth=1; depth<=16; ++depth) {
        Fixture f(1); LowHandoff low; low.Load(f.model.generation()); f.Ready(depth);
        f.native.countdown=depth;
        f.native.calls=0xfffffff0;
        // Keep the completion baseline aligned while exercising counter wrap.
        f.Ready(depth);
        unsigned updates=0;
        for (unsigned tick=0; tick<depth; ++tick) {
            if (f.native.countdown==1) f.Publish();
            const auto r=low.Begin(*f.model.parked(),f.native);
            Check(r && r->wakes_writer==(tick+1==depth), "only the final native slowdown tick wakes logic");
            Check(low.PrefixDone(*r), "one input prefix per native slowdown tick");
            --f.native.countdown; ++f.native.calls;
            Check(low.DriveDone(*r,f.native,1), "one native callback and wake increment per tick");
            if (r->wakes_writer) ++updates;
            Check(low.ConsumePrefix(f.native.frame,f.native.renderer) && low.ConsumeDriver(f.native.frame,f.native.calls),
                  "each slowdown tick settles both moved steps");
            ++f.native.frame;
        }
        Check(updates==1, "every native slowdown depth retains exactly one logic update");
    }
    {
        std::mutex gate; std::condition_variable cv; bool complete=false;
        std::unique_lock lock(gate);
        cv.notify_all();
        Check(!cv.wait_until(lock,std::chrono::steady_clock::now(),[&]{return complete;}),
              "early notification or spurious wake is not completion");
        complete=true; cv.notify_all();
        Check(cv.wait_until(lock,std::chrono::steady_clock::now(),[&]{return complete;}),
              "completion before blocking is not lost");
    }
    {
        Fixture f(3); f.Ready();
        Check(f.model.Eligible(f.native)==Gate::Original, "original never moves swap");
    }
    {
        Fixture f;
        Check(f.model.Eligible(f.native)==Gate::NoCompletion, "writer all-ones is not completion");
        f.Ready();
        Check(f.model.Eligible(f.native)==Gate::Ready, "genuine ready record");
        f.model.ReadBegin();
        Check(f.model.Eligible(f.native)==Gate::Reading, "no swap inside a reader");
        f.model.ReadEnd();
        f.Publish();
        Check(f.model.Eligible(f.native)==Gate::Pending, "no second publication before settlement");
        auto wrong=f.native; ++wrong.calls;
        Check(!f.model.Consume(wrong,f.native.callback), "late receipt has exact wake identity");
        Check(!f.model.Consume(f.native,f.native.callback+4), "foreign callback does not consume receipt");
        f.model.Retire();
        Check(f.model.HasReceipt(), "pause/unload cannot erase a successful swap receipt");
        f.native.frame += 120;
        Check(f.model.Consume(f.native,f.native.callback), "retired successful publication still settles");
        Check(!f.model.Consume(f.native,f.native.callback), "late swap skipped exactly once");
    }
    {
        Fixture f; f.Ready();
        const auto p=f.model.BeginPublication(f.native);
        Check(bool(p), "acquire publication lease");
        Check(!f.model.Load(2), "cannot reset a module with guest work executing");
        f.model.Retire();
        f.native.serial++; f.native.writer=f.native.published; f.native.published=f.native.finished;
        Check(f.model.Commit(*p,f.native,1), "retirement during native call still records its result");
        Check(f.model.Consume(f.native,f.native.callback), "retirement drains successful call");
        const auto generation=f.model.generation();
        Check(f.model.Load(2) && f.model.generation()>generation, "same DLL reload gets a new generation");
        Check(f.model.Eligible(f.native)==Gate::NoCompletion, "address reuse is not a completion");
    }
    {
        Fixture f; f.Ready(3);
        f.native.countdown=3;
        Check(f.model.Eligible(f.native)==Gate::Countdown, "preserve slowdown first interval");
        f.native.countdown=2; ++f.native.calls;
        Check(f.model.Eligible(f.native)==Gate::Countdown, "preserve slowdown middle interval");
        f.native.countdown=1; ++f.native.calls;
        Check(f.model.Eligible(f.native)==Gate::Ready, "publish only at native slowdown due interval");
        f.Publish(); Check(f.model.Consume(f.native,f.native.callback), "slowdown late settlement");
    }
    for (unsigned reason=0; reason<8; ++reason) {
        Fixture f; f.Ready();
        switch (reason) {
        case 0: ++f.native.serial; break;
        case 1: ++f.native.calls; break;
        case 2: ++f.native.renderer; break;
        case 3: f.native.writer=0x30000; break;
        case 4: f.native.busy=1; break;
        case 5: f.native.status=0; break;
        case 6: ++f.native.finished; break;
        case 7: ++f.native.cursor; break;
        }
        Check(!f.model.BeginPublication(f.native), "inconsistent native state rejects early swap");
    }
    {
        Fixture f;
        f.model.Logic(f.native); f.model.Commands(f.native.finished,f.native.serial);
        Check(!f.model.Park(f.native,f.native.calls,1,false), "partial command stream not published");
        Check(!f.model.Park(f.native,f.native.calls,0,true), "zero-depth special path not optimized");
        Check(!f.model.Park(f.native,f.native.calls,17,true), "unknown wait depth not optimized");
        Check(!f.model.Park(f.native,f.native.calls-1,1,true), "baseline race rejects completion");
    }
    {
        Fixture f; f.Ready(); f.model.Unpark();
        Check(!f.model.BeginPublication(f.native), "resumed producer invalidates completion");
        f.Ready(); f.model.Logic(f.native);
        Check(!f.model.BeginPublication(f.native), "next logic iteration invalidates old result");
    }
    for (unsigned failure=0; failure<4; ++failure) {
        Fixture f; f.Ready(); const auto p=f.model.BeginPublication(f.native);
        f.native.serial++; f.native.writer=f.native.published; f.native.published=f.native.finished;
        if(failure==0) --f.native.serial;
        if(failure==1) f.native.writer=f.native.published;
        if(failure==2) ++f.native.published;
        Check(!f.model.Commit(*p,f.native,failure==3?0:1), "partial/failed native swap is not accepted");
        Check(f.model.Eligible(f.native)==Gate::Inactive, "ownership mismatch disables optimization");
    }
    {
        Fixture f; f.Ready(); const auto p=f.Publish();
        Check(!f.model.Commit(p,f.native,1), "cannot commit publication twice");
        f.native.renderer++; f.native.frame += 600;
        Check(f.model.Consume(f.native,0x88051350), "settle old invocation despite later scene mutation");
    }
    std::cout<<checks<<" SDOJ frame-delay ownership checks passed\n";
}
int main() {
    try { RunChecks(); return 0; }
    catch (const std::exception& error) {
        std::cerr<<"SDOJ frame-delay check "<<checks<<" failed: "<<error.what()<<'\n';
        return 1;
    }
}
