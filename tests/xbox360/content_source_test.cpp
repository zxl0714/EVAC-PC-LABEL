#include <rex/system/xam/content_manager.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Require(bool value,const char* reason) {if (!value) throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv) {
    try {
        namespace fs=std::filesystem;
        using namespace rex;
        using namespace rex::system;
        using namespace rex::system::xam;
        Require(argc==2,"Use an isolated new fixture directory");
        const auto root=fs::absolute(argv[1]);Require(!fs::exists(root),"Fixture already exists");
        const auto source=root/"source",profile=root/"profile";
        const auto package=source/"0000000000000000/435A07D7/00000002/package";
        fs::create_directories(package);fs::create_directories(profile);
        {std::ofstream file(package/"asset.bin",std::ios::binary);file<<"authored fixture";}
        ContentManager manager(nullptr,profile);
        manager.SetReadOnlyContentRoot(source);
        XCONTENT_AGGREGATE_DATA dlc{};
        dlc.content_type=XContentType::kMarketplaceContent;dlc.title_id=0x435A07D7;dlc.xuid=0;dlc.set_file_name("package");
        Require(manager.ContentExists(0,dlc),"DLC was not resolved from its source");
        Require(manager.ListContent(1,0,XContentType::kMarketplaceContent,0x435A07D7).size()==1,"DLC enumeration failed");
        Require(manager.CreateContent("DLC",0,dlc)==X_ERROR_ACCESS_DENIED,"DLC creation allowed");
        Require(manager.WriteContentHeaderFile(0,dlc)==X_ERROR_ACCESS_DENIED,"DLC header write allowed");
        Require(manager.SetContentThumbnail(0,dlc,{1,2,3})==X_ERROR_ACCESS_DENIED,"DLC thumbnail write allowed");
        Require(manager.DeleteContent(0,dlc)==X_ERROR_ACCESS_DENIED,"DLC deletion allowed");
        Require(manager.UnmountAndDeleteContent(0,dlc)==X_ERROR_ACCESS_DENIED,"DLC unmount-and-delete allowed");
        Require(manager.InstallContent(source/"missing.stfs")==X_ERROR_ACCESS_DENIED,"DLC install allowed");
        auto save=dlc;save.content_type=XContentType::kSavedGame;save.xuid=0xB13EBABEBABEBABEULL;save.set_file_name("save");
        Require(manager.WriteContentHeaderFile(0xB13EBABEBABEBABEULL,save)==X_ERROR_SUCCESS,"Save header became read-only");
        Require(fs::is_regular_file(profile/"B13EBABEBABEBABE/435A07D7/Headers/00000001/save.header"),"Save escaped its profile");
        filesystem::HostPathDevice device("fixture",package,true);
        Require(device.Initialize(),"Read-only device initialization failed");
        auto* entry=device.ResolvePath("asset.bin");Require(entry && entry->is_read_only(),"DLC entry is writable");
        filesystem::File* file=nullptr;
        Require(entry->Open(filesystem::FileAccess::kFileWriteData,&file)!=X_STATUS_SUCCESS && !file,"DLC payload write allowed");
        Require(entry->Open(filesystem::FileAccess::kFileReadData,&file)==X_STATUS_SUCCESS && file,"DLC payload read failed");
        file->Destroy();
        std::ifstream input(package/"asset.bin",std::ios::binary);
        Require(std::string(std::istreambuf_iterator<char>(input),{})=="authored fixture","Source content changed");
        std::cout<<"Read-only DLC source and writable save isolation passed; synthetic data only\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
