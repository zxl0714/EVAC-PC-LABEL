// Render the production layout at representative sizes/DPI without opening a
// visible window. Installation states below are authored visual examples.
void SaveVisual(Renderer& renderer,IWICBitmap* bitmap,const fs::path& path,UINT width,UINT height) {
  ComPtr<IWICStream> stream;Check(renderer.wic->CreateStream(&stream),"Create visual check stream");
  Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Open visual check output");
  ComPtr<IWICBitmapEncoder> encoder;Check(renderer.wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Create visual check encoder");
  Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Initialize visual encoder");
  ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr),"Create visual frame");
  Check(frame->Initialize(nullptr),"Initialize visual frame");Check(frame->SetSize(width,height),"Set visual size");
  WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(frame->SetPixelFormat(&format),"Set visual pixels");
  Check(frame->WriteSource(bitmap,nullptr),"Write visual pixels");Check(frame->Commit(),"Commit visual frame");Check(encoder->Commit(),"Commit visual image");
}
void VisualChecks(const fs::path& output,bool initial_covers=false) {
  Require(!fs::exists(output) && fs::create_directory(output),"Visual output must be a fresh directory");
  const auto previous=language;
  std::vector<std::array<UINT,3>> sizes{{1080u,900u,96u},{720u,520u,144u}};
  if(initial_covers) {
    sizes.clear();
    for(const auto monitor:std::array{std::array{1920u,1040u,96u},std::array{1920u,1030u,120u},
                                     std::array{1920u,1020u,144u},std::array{2560u,1380u,144u}}) {
      const auto dpi=monitor[2];const auto scale=float(dpi)/96;
      const auto frame_width=int(34*scale),frame_height=int(40*scale);
      const auto window=cave::launcher::InitialCoverWindowSize(monitor[0],monitor[1],dpi,frame_width,frame_height,Catalogue().size());
      sizes.push_back({UINT((window.width-frame_width)/scale),UINT((window.height-frame_height)/scale),dpi});
    }
  }
  for(const auto current:{Language::Chinese,Language::English,Language::Japanese}) {
    language=current;
    for(const auto dimensions:sizes) {
      for(const auto covers:{false,true}) {
      if(initial_covers && !covers)continue;
      for(const auto selected:{std::size_t(4),Catalogue().size()-1}) {
      const auto dpi=dimensions[2];const auto scale=float(dpi)/96;
      const auto width=UINT(dimensions[0]*scale),height=UINT(dimensions[1]*scale);
      Renderer renderer;ComPtr<IWICBitmap> bitmap;
      Check(renderer.wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create visual bitmap");
      ComPtr<ID2D1RenderTarget> target;
      Check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create visual target");
      target->SetDpi(float(dpi),float(dpi));
      Layout layout{float(dimensions[0]),float(dimensions[1]),0};
      layout.covers=covers;
      layout.scroll=std::clamp(float(selected/layout.columns())*(layout.row_height()+layout.row_gap())+layout.row_height()-layout.viewport_height(),0.f,layout.max_scroll());
      std::vector<Row> rows(Catalogue().size());
      for(size_t index=0;index<rows.size();++index) {
        rows[index].inspection.state=Catalogue()[index].coming_soon?"coming_soon":"ready";
        rows[index].inspection.data_state=DataState::Verified;
      }
      if(covers) {
        rows[2].inspection.state="missing_data";
        rows[2].inspection.data_state=DataState::Missing;
      }
      target->BeginDraw();renderer.Draw(target.Get(),layout,rows,selected,-1,false,L"",{},true);
      Check(target->EndDraw(),"Render production visual layout");
      const auto prefix=std::string(covers?"covers-":"")+(selected==4?"":"last-");
      const auto path=output/(prefix+std::string(cave::launcher::LanguageCode(current))+"-"+std::to_string(dimensions[0])+"x"+std::to_string(dimensions[1])+"-"+std::to_string(dpi)+"dpi.png");
      SaveVisual(renderer,bitmap.Get(),path,width,height);
      std::vector<BYTE> pixels(std::size_t(width)*height*4);
      Check(bitmap->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data()),"Read visual pixels");
      const auto first_ink=[&](D2D1_RECT_F region) {
        for(auto y=UINT(region.top*scale);y<UINT(region.bottom*scale) && y<height;++y)
          for(auto x=UINT(region.left*scale);x<UINT(region.right*scale) && x<width;++x) {
            const auto p=&pixels[(std::size_t(y)*width+x)*4];
            if(std::abs(int(p[0])-240)+std::abs(int(p[1])-244)+std::abs(int(p[2])-243)>40)return float(y)/scale;
          }
        return -1.f;
      };
      const auto brand=first_ink(layout.brand());
      const auto first=layout.language_choice(0),last=layout.language_choice(2);
      const auto choices=first_ink({first.left,first.top,last.right,last.bottom});
      Require(brand>=0 && choices>=0 && std::abs(brand-choices)<=3,"Visible logo and language glyphs are not top-aligned");
      Require(first_ink({layout.margin(),layout.brand().bottom+1,layout.width-layout.margin(),layout.header()-2})<0,
              "Header gap still contains a permanent helper label");
      std::cout<<"visual "<<path.filename().string()<<" logo_top="<<brand<<" language_top="<<choices<<"\n";
      }
      }
    }
  }
  language=previous;
}
