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
void VisualChecks(const fs::path& output) {
  Require(!fs::exists(output) && fs::create_directory(output),"Visual output must be a fresh directory");
  const auto previous=language;
  for(const auto current:{Language::Chinese,Language::English,Language::Japanese}) {
    language=current;
    for(const auto dimensions:std::array{std::array{1080u,900u,96u},std::array{720u,520u,144u}}) {
      const auto dpi=dimensions[2];const auto scale=float(dpi)/96;
      const auto width=UINT(dimensions[0]*scale),height=UINT(dimensions[1]*scale);
      Renderer renderer;ComPtr<IWICBitmap> bitmap;
      Check(renderer.wic->CreateBitmap(width,height,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap),"Create visual bitmap");
      ComPtr<ID2D1RenderTarget> target;
      Check(renderer.factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&target),"Create visual target");
      target->SetDpi(float(dpi),float(dpi));
      Layout layout{float(dimensions[0]),float(dimensions[1]),0};
      constexpr std::size_t selected=4;
      layout.scroll=std::clamp(float(selected)*(layout.row_height()+12)+layout.row_height()-layout.viewport_height(),0.f,layout.max_scroll());
      std::vector<Row> rows(Catalogue().size());
      for(auto& row:rows)row.inspection.state="ready";
      target->BeginDraw();renderer.Draw(target.Get(),layout,rows,selected,-1,false,L"",{},true);
      Check(target->EndDraw(),"Render production visual layout");
      const auto path=output/(std::string(cave::launcher::LanguageCode(current))+"-"+std::to_string(dimensions[0])+"-"+std::to_string(dpi)+"dpi.png");
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
  language=previous;
}
