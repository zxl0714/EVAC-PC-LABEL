#include "launcher/input_settings.h"
#include "launcher/game_exit.h"
#include "common/config_update.h"
#include "common/controller_bindings.h"
#include "common/controller_ports.h"
#include "common/settings.h"
#include <commctrl.h>
#include <utility>

namespace cave::launcher::controls {
namespace {
constexpr wchar_t kClass[]=L"EvacInputSettings";
constexpr int kPreferredHeight=736;
enum Control {Keyboard=101,Controller,Scope,Devices,Bindings,Bind,Clear,Reset,Player,SwapPlayers};

struct Draft {
    std::string before;
    Ini original;
    std::vector<IniEdit> changes;
    explicit Draft(const fs::path& root):before(Ini::ReadText(EnsureConfig(root))),original(Ini::Parse(before)){}
    Ini Preview() const {return Ini::Parse(EditIni(before,changes));}
    void Put(std::string section,std::string_view key,std::string value={},bool erase=false) {
        const auto old=original.Get(section,key);
        std::erase_if(changes,[&](const auto& edit){return edit.section==section && edit.key==key;});
        if(erase ? old!=nullptr : !old || old->text!=value)
            changes.push_back({std::move(section),std::string(key),std::move(value),erase});
    }
    void Save(const fs::path& root) const {
        const auto preview=Preview();
        (void)ResolveSharedSettings(preview);
        for(const auto& game:Catalogue()) {
            (void)ResolveSettings(preview,game.id);
            (void)ControllerMappings(preview,game.id);
        }
        UpdateConfig(root,before,changes);
    }
};

struct Dialog {
    HWND window{},list{},device_control{},player_control{},status{},save{},bind{},clear{},reset{};
    fs::path root;
    const Game& game;
    Language language;
    Draft draft;
    bool game_only{},controller{},saved{},player_two{};
    int capture=-1;
    Settings keyboard_settings;
    PadMap defaults,pad;
    std::unique_ptr<InputDevices> input;
    std::vector<InputDevices::Device> devices;
    SDL_JoystickID selected_device{};
    std::array<std::uint32_t,2> seats{};
    std::string guid,last_guid,device_error;
    PadSnapshot previous,baseline;
    std::uint16_t first_exit_key{};
    std::string first_exit_pad;
    ULONGLONG scan_at{};
    int dpi=96;

    Dialog(const fs::path& path,const Game& title,Language lang):root(path),game(title),language(lang),draft(root){}
    const wchar_t* T(const wchar_t* zh,const wchar_t* en,const wchar_t* ja) const {
        switch(language){case Language::Chinese:return zh;case Language::Japanese:return ja;default:return en;}
    }
    std::string Section(std::string_view category) const {
        return game_only?"game."+std::string(game.id)+"."+std::string(category):std::string(category);
    }
    bool ExtraKeys() const {return game_only && game.id=="guwange";}
    static std::string_view KeyboardCategory(bool second) {return second?"keyboard_p2":"keyboard";}
    const Binding& KeyboardBinding(unsigned row) const {
        return row<16?keyboard_settings.Keyboard(player_two)[row]:keyboard_settings.KeyboardRightStick(player_two)[row-16];
    }
    unsigned GameRowCount() const {
        if(controller)return !game_only || game.backend=="ps4"?21:20;
        return ExtraKeys()?20:16;
    }
    bool ShowExit() const {return !game_only && (controller || !player_two);}
    unsigned RowCount() const {return GameRowCount()+(ShowExit()?2:0);}
    bool StateRow(unsigned row) const {return ShowExit() && row==GameRowCount()+1;}
    bool ShortcutRow(unsigned row) const {return ExitRow(row) || StateRow(row);}
    bool ExitRow(unsigned row) const {return ShowExit() && row==GameRowCount();}
    static bool Analog(unsigned row){return row>=16 && row<20;}
    std::string_view Key(unsigned row) const {return row<16?kButtons[row]:kRightStickButtons[row-16];}
    std::wstring Label(unsigned row) const {
        if(StateRow(row))return T(L"PS2 即时存档菜单",L"PS2 save state menu",L"PS2 クイックセーブメニュー");
        if(ExitRow(row))return T(L"退出游戏",L"Exit game",L"ゲーム終了");
        return GameLabel(row);
    }
    std::wstring GameLabel(unsigned row) const {
        switch(row) {
        case 0:return T(L"方向上",L"Up",L"上");
        case 1:return T(L"方向下",L"Down",L"下");
        case 2:return T(L"方向左",L"Left",L"左");
        case 3:return T(L"方向右",L"Right",L"右");
        case 4:case 5:case 6:case 7: {
            constexpr std::array xbox{L"A",L"B",L"X",L"Y"},playstation{L"×",L"○",L"□",L"△"};
            constexpr std::array common{L"A / ×",L"B / ○",L"X / □",L"Y / △"};
            return game_only?(game.backend=="xbox360"?xbox:playstation)[row-4]:common[row-4];
        }
        case 8:return game_only && game.backend=="ps4"?L"Options":L"Start";
        case 9:return !game_only?L"Back / Select":game.backend=="xbox360"?L"Back":
            game.backend=="ps4"?(controller?L"Share":T(L"触控板",L"Touchpad",L"タッチパッド")):L"Select";
        case 10:case 11:case 12:case 13:case 14:case 15: {
            constexpr std::array xbox{L"LB",L"RB",L"LT",L"RT",L"LS",L"RS"},other{L"L1",L"R1",L"L2",L"R2",L"L3",L"R3"};
            return (game_only && game.backend=="xbox360"?xbox:other)[row-10];
        }
        case 20:return T(L"触控板",L"Touchpad",L"タッチパッド");
        default:
            if(controller) {
                const std::array axes{T(L"左摇杆 X →",L"Left stick X →",L"左スティック X →"),T(L"左摇杆 Y ↓",L"Left stick Y ↓",L"左スティック Y ↓"),
                    T(L"右摇杆 X →",L"Right stick X →",L"右スティック X →"),T(L"右摇杆 Y ↓",L"Right stick Y ↓",L"右スティック Y ↓")};
                return axes.at(row-16);
            }
            const std::array shikigami{T(L"式神 ↑",L"Shikigami ↑",L"式神 ↑"),T(L"式神 ↓",L"Shikigami ↓",L"式神 ↓"),
                T(L"式神 ←",L"Shikigami ←",L"式神 ←"),T(L"式神 →",L"Shikigami →",L"式神 →")};
            return shikigami.at(row-16);
        }
    }
    std::wstring PadLabel(std::string_view value) const {
        if(value.empty())return L"—";
        const auto axis=value.find('a');
        if(axis!=value.npos)return std::wstring(T(L"轴 ",L"Axis ",L"軸 "))+FromUtf8(value.substr(axis+1)).native()+
            (axis?L" "+FromUtf8(value.substr(0,axis)).native():L"");
        if(value.front()=='b')return std::wstring(T(L"按键 ",L"Button ",L"ボタン "))+FromUtf8(value.substr(1)).native();
        if(value.front()=='h')return std::wstring(T(L"方向帽 ",L"Hat ",L"ハット "))+FromUtf8(value.substr(1)).native();
        return FromUtf8(value).native();
    }
    std::wstring ShortcutLabel(const Ini& ini,bool state) const {
        if(!controller) {
            const auto* entry=state?ini.Get("collection",kStateMenuKey):ExitKeySetting(ini);
            const auto keys=entry?ParseExitKeys(entry->text).value_or(ExitKeys{}):(state?ExitKeys{kStateMenuDefaultKey}:ExitKeys{});
            return keys[0]?FromUtf8(ExitKeyText(keys)).native():L"—";
        }
        if(guid.empty())return L"—";
        if(const auto* entry=ini.Get(state?kStateMenuController:kExitControllerSection,guid)) {
            const auto binding=ParseExitPadBinding(entry->text);
            if(!binding || binding->front().empty())return L"—";
            const auto label=PadLabel((*binding)[0]);
            return (*binding)[1].empty()?label:label+L" + "+PadLabel((*binding)[1]);
        }
        if(state)return L"—";
        const auto* entry=ini.Get("collection",kExitButtonsSetting);
        const auto buttons=entry?ParseExitButtons(entry->text).value_or(ExitButtons{}):ExitButtons{};
        std::wstring label;
        for(const auto button:buttons) {
            if(!pad.contains(std::string(button)))return L"—";
            if(!label.empty())label+=L" + ";
            label+=Label(unsigned(std::ranges::find(kPadKeys,button)-kPadKeys.begin()));
        }
        return label.empty()?L"—":label;
    }
    void Status(std::wstring_view text) {SetWindowTextW(status,std::wstring(text).c_str());}
    void IdleStatus() {
        if(controller && !device_error.empty())Status(T(L"无法读取手柄设备",L"Controller input is unavailable",L"コントローラーを読み取れません"));
        else if(controller && guid.empty())Status(T(L"连接手柄或摇杆后即可配置",L"Connect a controller or arcade stick",L"コントローラーを接続してください"));
        else Status(T(L"双击绑定改键 · 按下设备按键试用",L"Double-click to edit · Press an input to test",L"ダブルクリックで変更 · 入力してテスト"));
    }
    int SelectedRow() const {return ListView_GetNextItem(list,-1,LVNI_SELECTED);}
    void EnableActions() {
        const bool available=!controller || !guid.empty();
        for(auto control:{bind,clear})EnableWindow(control,available && SelectedRow()>=0);
        EnableWindow(save,!draft.changes.empty());
    }
    void Refresh(int selection=0) {
        const auto ini=draft.Preview();
        if(controller) {
            const auto mappings=ControllerMappings(ini,game_only?game.id:std::string_view{});
            const auto found=mappings.find(guid);
            pad=found==mappings.end()?defaults:ParsePadMap(found->second);
        } else {
            keyboard_settings=game_only?ResolveSettings(ini,game.id):ResolveSharedSettings(ini);
        }
        SendMessageW(list,WM_SETREDRAW,FALSE,0);
        ListView_SetColumnWidth(list,1,MulDiv(game_only?220:320,dpi,96));
        ListView_SetColumnWidth(list,2,MulDiv(game_only?100:0,dpi,96));
        ListView_DeleteAllItems(list);
        for(unsigned row=0;row<RowCount();++row) {
            auto title=Label(row);
            LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=int(row);item.pszText=title.data();
            SendMessageW(list,LVM_INSERTITEMW,0,reinterpret_cast<LPARAM>(&item));
            std::wstring value;
            if(ShortcutRow(row))value=ShortcutLabel(ini,StateRow(row));
            else if(controller) {
                const auto found=pad.find(std::string(kPadKeys[row]));
                value=PadLabel(found==pad.end()?std::string_view{}:found->second);
            } else value=KeyboardBinding(row).keys.empty()?L"—":FromUtf8(KeyboardBinding(row).text).native();
            item.iSubItem=1;item.pszText=value.data();SendMessageW(list,LVM_SETITEMTEXTW,row,reinterpret_cast<LPARAM>(&item));
            std::wstring source;
            if(game_only) {
                const auto* override_value=controller?ini.Get(Section("controller"),guid):ini.Get(Section(KeyboardCategory(player_two)),Key(row));
                source=override_value?T(L"本游戏",L"This game",L"このゲーム"):T(L"继承",L"Inherited",L"共通設定");
            }
            item.iSubItem=2;item.pszText=source.data();SendMessageW(list,LVM_SETITEMTEXTW,row,reinterpret_cast<LPARAM>(&item));
        }
        const auto row=std::clamp(selection,0,int(RowCount())-1);
        ListView_SetItemState(list,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(list,row,FALSE);
        SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
        SetWindowTextW(reset,game_only?T(L"恢复共用设置",L"Use shared bindings",L"共通設定に戻す"):
                                      T(L"恢复默认",L"Reset to defaults",L"初期設定に戻す"));
        EnableActions();IdleStatus();
    }
    void SelectDevice(SDL_JoystickID id) {
        capture=-1;selected_device=0;guid.clear();defaults.clear();previous={};
        if(input && input->Select(id)) {
            const auto found=std::find_if(devices.begin(),devices.end(),[&](const auto& item){return item.id==id;});
            if(found!=devices.end() && ControllerGuid(found->guid)) {
                selected_device=id;last_guid=guid=found->guid;defaults=input->Defaults(id);previous=input->Read();
            }
        }
        Refresh(SelectedRow());
    }
    bool Swapped(const Ini& ini) const {
        return (game_only?ResolveSettings(ini,game.id):ResolveSharedSettings(ini)).swap_players;
    }
    void Scan(bool refresh=false) {
        if(!input)return;
        auto connected=input->List();
        if(!refresh && connected==devices)return;
        devices=std::move(connected);
        const auto ini=draft.Preview();
        const bool swapped=Swapped(ini);
        const auto mappings=ControllerMappings(ini,game_only?game.id:std::string_view{});
        const auto eligible=[&](const auto& device){return device.gamepad || mappings.contains(device.guid);};
        for(auto& id:seats) {
            const auto found=std::ranges::find(devices,id,&InputDevices::Device::id);
            if(found==devices.end() || !eligible(*found))id=0;
        }
        for(const auto& device:devices) {
            if(!eligible(device) || std::ranges::find(seats,device.id)!=seats.end())continue;
            const auto slot=AvailableControllerPort(seats,device.player_index);
            if(slot<0)break;
            seats[slot]=device.id;
        }
        EnableWindow(GetDlgItem(window,SwapPlayers),swapped || (seats[0] && seats[1]));
        int choice=-1;
        SendMessageW(device_control,CB_RESETCONTENT,0,0);
        for(std::size_t index=0;index<devices.size();++index) {
            const auto& device=devices[index];
            auto name=FromUtf8(device.name).native();
            for(unsigned seat=0;seat<seats.size();++seat)if(seats[seat]==device.id)
                name=std::to_wstring(ControllerPort(seat,swapped)+1)+L"P · "+name;
            SendMessageW(device_control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
            if(device.id==selected_device || (choice<0 && device.guid==last_guid))choice=int(index);
        }
        if(devices.empty())SendMessageW(device_control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(T(L"未连接设备",L"No device connected",L"未接続")));
        choice=std::max(0,choice);
        SendMessageW(device_control,CB_SETCURSEL,choice,0);
        const auto id=devices.empty()?0:devices[choice].id;
        if(id!=selected_device)SelectDevice(id);
    }
    void ChangeMode(bool pad_mode) {
        capture=-1;controller=pad_mode;
        CheckRadioButton(window,Keyboard,Controller,controller?Controller:Keyboard);
        ShowWindow(device_control,controller?SW_SHOW:SW_HIDE);
        ShowWindow(GetDlgItem(window,SwapPlayers),controller?SW_SHOW:SW_HIDE);
        ShowWindow(player_control,controller?SW_HIDE:SW_SHOW);
        if(controller && !input && device_error.empty()) {
            try {input=std::make_unique<InputDevices>(root);input->Update();}
            catch(const std::exception& error){device_error=error.what();}
        }
        if(controller && input)Scan(true);
        if(controller && devices.empty()) {
            SendMessageW(device_control,CB_RESETCONTENT,0,0);
            SendMessageW(device_control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(T(L"未连接设备",L"No device connected",L"未接続")));
            SendMessageW(device_control,CB_SETCURSEL,0,0);
        }
        Refresh();
    }
    void ApplyBindings(int row,std::span<const IniEdit> edits) {
        auto old=draft.changes;
        try {
            for(const auto& edit:edits)draft.Put(edit.section,edit.key,edit.value,edit.erase);
            const auto preview=draft.Preview();
            if(game_only) (void)ResolveSettings(preview,game.id);
            else {
                (void)ResolveSharedSettings(preview);
                for(const auto& title:Catalogue()) {
                    try {(void)ResolveSettings(preview,title.id);}
                    catch(const std::exception& error) {
                        throw std::runtime_error(std::string(title.DisplayName())+" · "+error.what());
                    }
                }
            }
            Refresh(row);
        }
        catch(...) {draft.changes=std::move(old);Refresh(row);throw;}
    }
    void ResetBindings(int row) {
        const auto keyboard=Section("keyboard"), second=Section("keyboard_p2"), controllers=Section("controller");
        const auto preview=draft.Preview();
        std::vector<IniEdit> edits;
        for(const auto& [address,value]:preview.Values()) {
            const auto& [section,key]=address;
            const bool exit_binding=section==kExitControllerSection || section==kStateMenuController || (section=="collection" &&
                (key==kStateMenuKey || key==kExitKeySetting || key==kPreviousExitKeySetting || key==kExitButtonsSetting));
            if(section==keyboard || section==second || section==controllers || (section==Section("gamepad") && key=="swap_players") || (!game_only && exit_binding))
                edits.push_back({address.first,address.second,{},true});
        }
        ApplyBindings(row,edits);
    }
    void MoveStatus(const std::wstring& key,unsigned row,const std::wstring& released) {
        Status(key+L"  →  "+Label(row)+T(L"  ·  原位置 ",L"  ·  Moved from ",L"  ·  移動元 ")+released);
    }
    void SetPad(unsigned row,std::string source) {
        if(guid.empty())return;
        auto map=pad;
        const auto target=std::string(kPadKeys[row]);
        std::wstring released;
        if(!source.empty())for(unsigned i=0;i<kPadKeys.size();++i) {
            if(i==row)continue;
            const auto other=map.find(std::string(kPadKeys[i]));
            if(other!=map.end() && other->second==source) {
                if(!released.empty())released+=L" / ";
                released+=Label(i);
                map.erase(other);
            }
        }
        if(source.empty())map.erase(target);else map[target]=source;
        if(map==pad)return;
        draft.Put(Section("controller"),guid,SerializePadMap(map));
        Refresh(int(row));
        if(!released.empty())MoveStatus(PadLabel(source),row,released);
    }
    void Begin() {
        capture=SelectedRow();if(capture<0 || (controller && guid.empty())){capture=-1;return;}
        if(controller){input->Update();baseline=previous=input->Read();}
        if(ShortcutRow(capture)) {
            first_exit_key=0;
            first_exit_pad.clear();
            Status(T(L"按一下录入单键，按住一个再按另一个录入组合键",
                L"Tap one input, or hold one and press another",L"1つ押して離すか、押したまま別の入力を押してください"));
            return;
        }
        Status(controller && Analog(capture)?Label(capture)+T(L"，请按箭头拨动",L" · Move in the shown direction",L" · 矢印の方向に操作"):
            std::wstring(T(L"按下要绑定的键 · Esc 取消",L"Press an input · Esc cancels",L"入力してください · Esc で取消")));
    }
    void SetShortcutBinding(int row,std::string value) {
        if(controller)draft.Put(std::string(StateRow(row)?kStateMenuController:kExitControllerSection),guid,std::move(value));
        else draft.Put("collection",StateRow(row)?kStateMenuKey:kExitKeySetting,std::move(value));
        Refresh(row);
    }
    void InputExitKey(std::uint16_t key,bool released) {
        key=ExitVirtualKey(key);
        if(released && key!=first_exit_key)return;
        if(KeyName(key).empty()) {
            Status(T(L"此键不可绑定",L"This key cannot be bound",L"このキーは割り当てできません"));
            return;
        }
        const ExitKeys held{first_exit_key?first_exit_key:key,key};
        const auto pressed=released?std::span<const std::uint16_t>{}:std::span<const std::uint16_t>{held};
        if(const auto binding=RecordExitBinding(first_exit_key,pressed))
            SetShortcutBinding(std::exchange(capture,-1),ExitKeyText(*binding));
    }
    void InputKey(unsigned code) {
        const auto name=KeyName(static_cast<std::uint16_t>(code));
        if(name.empty()){Status(T(L"此键不可绑定",L"This key cannot be bound",L"このキーは割り当てできません"));return;}
        const auto row=unsigned(std::exchange(capture,-1));
        try {
            const auto section=Section(KeyboardCategory(player_two));
            std::vector<IniEdit> edits{{section,std::string(Key(row)),name}};
            std::wstring released;
            const auto move=[&](const Settings& settings,std::string_view game_id,bool overrides_only) {
                const std::string prefix=game_id.empty()?"":"game."+std::string(game_id)+".";
                for(bool second:{false,true}) for(unsigned i=0;i<(game_id=="guwange"?20u:16u);++i) {
                    if(second==player_two && i==row)continue;
                    const auto& binding=i<16?settings.Keyboard(second)[i]:settings.KeyboardRightStick(second)[i-16];
                    if(overrides_only && !binding.section.starts_with(prefix))continue;
                    if(std::ranges::find(binding.keys,code)==binding.keys.end())continue;
                    std::string remaining;
                    for(const auto key:binding.keys)if(key!=code) {
                        if(!remaining.empty())remaining+=", ";
                        remaining+=KeyName(key);
                    }
                    edits.push_back({prefix+std::string(KeyboardCategory(second)),std::string(Key(i)),
                                     remaining.empty()?"none":std::move(remaining)});
                    released=(second?L"2P ":L"1P ")+GameLabel(i);
                }
            };
            move(keyboard_settings,game_only?game.id:std::string_view{},false);
            if(!game_only) {
                const auto ini=draft.Preview();
                for(const auto& title:Catalogue()) {
                    const auto scope="game."+std::string(title.id)+"."+section;
                    if(!ini.Get(scope,Key(row)))move(ResolveSettings(ini,title.id),title.id,true);
                }
            }
            ApplyBindings(int(row),edits);
            if(!released.empty())MoveStatus(FromUtf8(name).native(),row,released);
        }
        catch(const std::exception& error){Status(FromUtf8(error.what()).native());}
    }
    void Tick() {
        if(!controller || !input)return;
        input->Update();
        const auto now=GetTickCount64();
        if(now>=scan_at){scan_at=now+250;Scan();}
        if(guid.empty())return;
        auto state=input->Read();
        if(GetForegroundWindow()==window) {
            if(capture>=0) {
                if(ShortcutRow(capture)) {
                    const auto pressed=PressedExitPadInputs(baseline,state);
                    if(const auto binding=RecordExitBinding<std::string>(first_exit_pad,pressed))
                        SetShortcutBinding(std::exchange(capture,-1),(*binding)[1].empty()?(*binding)[0]:(*binding)[0]+','+(*binding)[1]);
                } else {
                    const auto source=CapturePad(previous,state,baseline,Analog(capture),capture==12 || capture==13);
                    if(!source.empty())SetPad(unsigned(std::exchange(capture,-1)),source);
                }
            } else {
                bool matched=false;
                for(unsigned row=0;row<kPadKeys.size();++row) {
                    const auto mapped=pad.find(std::string(kPadKeys[row]));
                    if(mapped!=pad.end() && PadSourceActive(mapped->second,state,row==12 || row==13) &&
                       !PadSourceActive(mapped->second,previous,row==12 || row==13)) {
                        Status(PadLabel(mapped->second)+L"  →  "+Label(row));matched=true;
                    }
                }
                if(!matched)if(const auto source=CapturePad(previous,state,previous,false,false);!source.empty())
                    Status(PadLabel(source)+T(L"  ·  未绑定",L"  ·  Unassigned",L"  ·  未割り当て"));
            }
        }
        previous=std::move(state);
    }
    void Command(unsigned id,unsigned action) {
        const auto row=SelectedRow();
        switch(id) {
        case Keyboard:case Controller:ChangeMode(id==Controller);break;
        case Scope:if(action==CBN_SELCHANGE){capture=-1;game_only=SendDlgItemMessageW(window,Scope,CB_GETCURSEL,0,0)==1;if(controller)Scan(true);Refresh();}break;
        case Player:if(action==CBN_SELCHANGE){capture=-1;player_two=SendMessageW(player_control,CB_GETCURSEL,0,0)==1;Refresh();}break;
        case Devices:if(action==CBN_SELCHANGE){const auto i=SendMessageW(device_control,CB_GETCURSEL,0,0);if(i>=0 && std::size_t(i)<devices.size())SelectDevice(devices[i].id);}break;
        case SwapPlayers:{
            capture=-1;
            const auto ini=draft.Preview();
            const bool swap=!Swapped(ini);
            const bool inherited=game_only && ResolveSharedSettings(ini).swap_players;
            draft.Put(Section("gamepad"),"swap_players",swap?"true":"false",swap==inherited);
            Scan(true);Refresh(row);break;
        }
        case Bind:Begin();break;
        case Clear:
            if(row<0)break;
            capture=-1;
            if(ShortcutRow(row))SetShortcutBinding(row,"none");
            else if(controller)SetPad(row,{});
            else {
                const std::array edits{IniEdit{Section(KeyboardCategory(player_two)),std::string(Key(row)),"none"}};
                ApplyBindings(row,edits);
            }
            break;
        case Reset:capture=-1;ResetBindings(row);if(controller)Scan(true);break;
        case IDOK:if(!draft.changes.empty()){draft.Save(root);saved=true;DestroyWindow(window);}break;
        case IDCANCEL:DestroyWindow(window);break;
        }
    }
    bool Filter(const MSG& message) {
        if(message.hwnd!=window && !IsChild(window,message.hwnd))return false;
        const bool pressed=message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN;
        const bool released=message.message==WM_KEYUP || message.message==WM_SYSKEYUP;
        if(capture>=0 && !controller && ShortcutRow(capture) && (pressed || released)) {
            if(released || !(message.lParam&(1LL<<30)))InputExitKey(std::uint16_t(message.wParam),released);
            return true;
        }
        if(!pressed)return false;
        if(capture>=0 && message.wParam==VK_ESCAPE) {
            capture=-1;IdleStatus();return true;
        }
        if(capture>=0 && !controller && (message.message==WM_KEYDOWN || message.wParam==VK_MENU)) {
            if(!(message.lParam&(1LL<<30)))InputKey(unsigned(message.wParam));
            return true;
        }
        if(capture<0 && !controller && message.message==WM_KEYDOWN) {
            for(unsigned row=0;row<GameRowCount();++row)if(std::ranges::find(KeyboardBinding(row).keys,message.wParam)!=KeyboardBinding(row).keys.end())
                Status(FromUtf8(KeyName(static_cast<std::uint16_t>(message.wParam))).native()+L"  →  "+Label(row));
        }
        return false;
    }
    static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,LONG_PTR(self));}
        if(!self)return DefWindowProcW(hwnd,msg,w,l);
        try {
            if(msg==WM_COMMAND){self->Command(LOWORD(w),HIWORD(w));return 0;}
            if(msg==WM_TIMER){self->Tick();return 0;}
            if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
            if(msg==WM_NOTIFY && reinterpret_cast<NMHDR*>(l)->idFrom==Bindings) {
                if(reinterpret_cast<NMHDR*>(l)->code==NM_DBLCLK)self->Begin();
                else if(reinterpret_cast<NMHDR*>(l)->code==LVN_ITEMCHANGED)self->EnableActions();
                return 0;
            }
        }catch(const std::exception& error){self->capture=-1;self->Status(FromUtf8(error.what()).native());return 0;}
        return DefWindowProcW(hwnd,msg,w,l);
    }
};
}

bool Show(HWND owner,const fs::path& root,const Game& game,Language language) {
    Dialog dialog(root,game,language);
    const auto instance=GetModuleHandleW(nullptr);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.lpfnWndProc=Dialog::Proc;wc.hInstance=instance;wc.lpszClassName=kClass;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassW(&wc);
    const auto dpi=GetDpiForWindow(owner);
    dialog.dpi=int(dpi);
    const auto scale=[&](int value){return MulDiv(value,int(dpi),96);};
    MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor))throw std::runtime_error("Cannot find input settings display");
    const auto work=monitor.rcWork;
    RECT parent{};GetWindowRect(owner,&parent);
    const auto t=[&](auto zh,auto en,auto ja){return dialog.T(zh,en,ja);};
    const int height=std::clamp(MulDiv(work.bottom-work.top,96,int(dpi))-52,272,kPreferredHeight);
    constexpr DWORD style=WS_CAPTION|WS_SYSMENU,extended=WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT;
    RECT outer{0,0,scale(596),scale(height)};AdjustWindowRectExForDpi(&outer,style,FALSE,extended,dpi);
    const auto width=outer.right-outer.left,physical_height=outer.bottom-outer.top;
    const auto x=std::clamp((parent.left+parent.right-width)/2,work.left,std::max(work.left,work.right-width));
    const auto y=std::clamp((parent.top+parent.bottom-physical_height)/2,work.top,std::max(work.top,work.bottom-physical_height));
    const auto hwnd=CreateWindowExW(extended,kClass,t(L"输入设置",L"Input settings",L"入力設定"),style,x,y,width,physical_height,owner,nullptr,instance,&dialog);
    if(!hwnd)throw std::runtime_error("Cannot create input settings window");
    struct Cleanup {HWND owner,window;HFONT font{};bool modal{};
        ~Cleanup(){if(IsWindow(window))DestroyWindow(window);if(font)DeleteObject(font);if(modal){EnableWindow(owner,TRUE);SetForegroundWindow(owner);}}
    } cleanup{owner,hwnd};
    cleanup.font=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
    const auto add=[&](const wchar_t* type,const wchar_t* text,DWORD flags,int x,int y,int w,int h,int id,DWORD ex=0) {
        const auto control=CreateWindowExW(ex,type,text,WS_CHILD|WS_VISIBLE|flags,scale(x),scale(y),scale(w),scale(h),hwnd,
            reinterpret_cast<HMENU>(INT_PTR(id)),instance,nullptr);
        if(!control)throw std::runtime_error("Cannot create input settings controls");
        SendMessageW(control,WM_SETFONT,WPARAM(cleanup.font),TRUE);
        return control;
    };
    add(L"BUTTON",t(L"键盘",L"Keyboard",L"キーボード"),WS_TABSTOP|WS_GROUP|BS_AUTORADIOBUTTON,20,15,105,26,Keyboard);
    add(L"BUTTON",t(L"手柄 / 摇杆",L"Controller / stick",L"コントローラー"),BS_AUTORADIOBUTTON,128,15,158,26,Controller);
    CheckRadioButton(hwnd,Keyboard,Controller,Keyboard);
    const auto scope=add(L"COMBOBOX",L"",WS_GROUP|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,309,14,267,180,Scope);
    SendMessageW(scope,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t(L"所有游戏",L"All games",L"すべてのゲーム")));
    SendMessageW(scope,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t(L"当前游戏",L"Current game",L"現在のゲーム")));
    SendMessageW(scope,CB_SETCURSEL,0,0);
    dialog.device_control=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,20,47,398,180,Devices);
    const auto swap=add(L"BUTTON",t(L"交换 1P / 2P",L"Swap 1P / 2P",L"1P / 2P 入替"),WS_TABSTOP,430,46,146,27,SwapPlayers);
    EnableWindow(swap,FALSE);ShowWindow(swap,SW_HIDE);
    ShowWindow(dialog.device_control,SW_HIDE);
    dialog.player_control=add(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,20,47,210,120,Player);
    SendMessageW(dialog.player_control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t(L"玩家 1（1P）",L"Player 1",L"プレイヤー 1")));
    SendMessageW(dialog.player_control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t(L"玩家 2（2P）",L"Player 2",L"プレイヤー 2")));
    SendMessageW(dialog.player_control,CB_SETCURSEL,0,0);
    dialog.list=add(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,20,82,556,height-212,Bindings,WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(dialog.list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(dialog.list,RGB(255,255,255));ListView_SetTextColor(dialog.list,RGB(35,49,45));
    const std::array titles{t(L"操作",L"Action",L"操作"),t(L"绑定",L"Binding",L"割り当て"),t(L"来源",L"Source",L"適用範囲")};
    constexpr std::array column_widths{210,220,100};
    for(int n=0;n<3;++n){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(titles[n]);column.cx=scale(column_widths[n]);SendMessageW(dialog.list,LVM_INSERTCOLUMNW,n,reinterpret_cast<LPARAM>(&column));}
    dialog.bind=add(L"BUTTON",t(L"改键",L"Bind",L"変更"),WS_TABSTOP,20,height-120,78,28,Bind);
    dialog.clear=add(L"BUTTON",t(L"清除",L"Clear",L"解除"),WS_TABSTOP,105,height-120,74,28,Clear);
    dialog.reset=add(L"BUTTON",L"",WS_TABSTOP,186,height-120,166,28,Reset);
    dialog.status=add(L"STATIC",L"",SS_PATHELLIPSIS,20,height-81,556,25,0);
    add(L"BUTTON",t(L"取消",L"Cancel",L"キャンセル"),WS_TABSTOP,385,height-42,90,29,IDCANCEL);
    dialog.save=add(L"BUTTON",t(L"保存",L"Save",L"セーブ"),WS_TABSTOP|BS_DEFPUSHBUTTON,486,height-42,90,29,IDOK);
    dialog.ChangeMode(false);
    EnableWindow(owner,FALSE);cleanup.modal=true;
    ShowWindow(hwnd,SW_SHOW);SetFocus(dialog.list);SetTimer(hwnd,1,40,nullptr);
    MSG message{};
    while(IsWindow(hwnd)) {
        const auto result=GetMessageW(&message,nullptr,0,0);
        if(result<=0){if(result==0)PostQuitMessage(int(message.wParam));break;}
        if(!dialog.Filter(message) && !IsDialogMessageW(hwnd,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    return dialog.saved;
}
}
