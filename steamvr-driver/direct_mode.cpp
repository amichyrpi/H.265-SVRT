#include "direct_mode.h"
#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <limits>

using Microsoft::WRL::ComPtr;
static void debug(const char *s){char line[2304];std::snprintf(line,sizeof(line),"SVRT: %s",s);OutputDebugStringA(line);OutputDebugStringA("\n");if(vr::VRDriverLog())vr::VRDriverLog()->Log(line);}
static void debugf(const char *format,...){char message[2048];va_list args;va_start(args,format);std::vsnprintf(message,sizeof(message),format,args);va_end(args);debug(message);}
SvrtDirectMode::SvrtDirectMode()=default;
SvrtDirectMode::~SvrtDirectMode(){Stop();}
bool SvrtDirectMode::EnsureDevice(){std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);return EnsureDeviceLocked();}
bool SvrtDirectMode::EnsureDeviceLocked(){
  if(device_&&context_) return true;
  UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;D3D_FEATURE_LEVEL level;
  HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&context_);
  if(FAILED(hr)){debugf("D3D11CreateDevice failed: 0x%08lx",(unsigned long)hr);return false;}
  return true;
}
uint64_t SvrtDirectMode::GraphicsAdapterLuid() const{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!device_) return 0;
  ComPtr<IDXGIDevice> dxgi_device;
  if(FAILED(device_.As(&dxgi_device))) return 0;
  ComPtr<IDXGIAdapter> adapter;
  if(FAILED(dxgi_device->GetAdapter(&adapter))) return 0;
  DXGI_ADAPTER_DESC desc{};
  if(FAILED(adapter->GetDesc(&desc))) return 0;
  uint64_t luid=0;
  static_assert(sizeof(desc.AdapterLuid)==sizeof(luid),"unexpected LUID size");
  std::memcpy(&luid,&desc.AdapterLuid,sizeof(luid));
  return luid;
}
bool SvrtDirectMode::Start(const std::string &host,uint16_t port,unsigned fps,unsigned bitrate,const std::string &ffmpeg,const std::string &encoder){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if (running_) return true;
  host_=host;port_=port;fps_=fps?fps:60;bitrate_=bitrate?bitrate:12;ffmpeg_=ffmpeg.empty()?"ffmpeg.exe":ffmpeg;encoder_=encoder.empty()?"hevc_nvenc":encoder;
  encoder_failed_=false; accepting_=false;if(!EnsureDeviceLocked()){encoder_failed_=true;return false;}debugf("direct mode ready: receiver=%s:%u fps=%u bitrate=%uM encoder=%s",host_.c_str(),port_,fps_,bitrate_,encoder_.c_str());running_=true;accepting_=true;worker_=std::thread(&SvrtDirectMode::EncoderThread,this);return true;
}
void SvrtDirectMode::SetReceiverAvailable(bool available){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  const bool was_available=receiver_available_.exchange(available);
  if(available&&!was_available)encoder_failed_=false;
  if(!available){
    disconnect_requested_=true;
    // Cancel a pipe write immediately when the receiver disappears. Without
    // this, vrserver can wait forever for FFmpeg and make the desktop appear
    // frozen during disconnect or SteamVR shutdown.
    if(worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
  }
  ready_.notify_one();
}
void SvrtDirectMode::Stop(){
  std::unique_lock<std::mutex> lifecycle(lifecycle_mutex_);
  accepting_=false;
  if(!running_.exchange(false)){return;}
  // A synchronous WriteFile to FFmpeg's stdin can block indefinitely when the
  // receiver disappears.  Stop the reader first: this breaks that write and
  // lets the SteamVR shutdown thread join the worker promptly.
  if(worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
  if(process_) TerminateProcess(process_, 0);
  if(extra_process_) TerminateProcess(extra_process_, 0);
  ready_.notify_all();
  if(worker_.joinable())worker_.join();
  CloseEncoder();
  std::lock_guard<std::mutex> l(mutex_);textures_.clear();slots_.clear();frame_.clear();extra_frame_.clear();context_.Reset();device_.Reset();
}
void SvrtDirectMode::CreateSwapTextureSet(uint32_t pid,const SwapTextureSetDesc_t *d,SwapTextureSet_t *out){
  *out={};if(!device_||!d)return;debugf("creating swap textures: pid=%u size=%ux%u format=%u samples=%u",pid,d->nWidth,d->nHeight,d->nFormat,d->nSampleCount);D3D11_TEXTURE2D_DESC td{};td.Width=d->nWidth;td.Height=d->nHeight;td.MipLevels=1;td.ArraySize=1;td.Format=(DXGI_FORMAT)d->nFormat;td.SampleDesc.Count=std::max(1u,d->nSampleCount);td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
  std::lock_guard<std::mutex> l(mutex_);Texture created[3];uint64_t keys[3]{};for(int i=0;i<3;i++){ComPtr<ID3D11Texture2D> tex;if(FAILED(device_->CreateTexture2D(&td,nullptr,&tex)))return;ComPtr<IDXGIResource> dxgi;if(FAILED(tex.As(&dxgi)))return;HANDLE h=nullptr;if(FAILED(dxgi->GetSharedHandle(&h))||!h)return;keys[i]=(uint64_t)(uintptr_t)h;created[i]=Texture{pid,keys[0],tex};}for(int i=0;i<3;i++){created[i].group=keys[0];textures_[keys[i]]=std::move(created[i]);out->rSharedTextureHandles[i]=(vr::SharedTextureHandle_t)keys[i];}out->unTextureFlags=0;
}
void SvrtDirectMode::DestroySwapTextureSet(vr::SharedTextureHandle_t h){std::lock_guard<std::mutex> l(mutex_);auto it=textures_.find((uint64_t)h);if(it==textures_.end())return;uint64_t group=it->second.group;for(auto p=textures_.begin();p!=textures_.end();){if(p->second.group==group)p=textures_.erase(p);else ++p;}}
void SvrtDirectMode::DestroyAllSwapTextureSets(uint32_t pid){std::lock_guard<std::mutex> l(mutex_);for(auto p=textures_.begin();p!=textures_.end();){if(p->second.pid==pid)p=textures_.erase(p);else ++p;}}
void SvrtDirectMode::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t[2],uint32_t (*indices)[2]){std::lock_guard<std::mutex> l(mutex_);next_[0]=(next_[0]+1)%3;next_[1]=(next_[1]+1)%3;(*indices)[0]=next_[0];(*indices)[1]=next_[1];}
void SvrtDirectMode::SubmitLayer(const SubmitLayerPerEye_t (&eyes)[2]){std::lock_guard<std::mutex> l(mutex_);submitted_[0]=eyes[0].hTexture;submitted_[1]=eyes[1].hTexture;}
static constexpr unsigned kStagingSlots=3;
/* The dual hardware decoders sustain about 46-48 native frames/s, with brief
   scheduling dips.  Pacing one frame below that floor prevents TCP/decoder
   queues from accumulating latency during long sessions. */
static constexpr unsigned kNativeStreamFps=45;
bool SvrtDirectMode::EnsureGpuConversion(unsigned w,unsigned h,DXGI_FORMAT fmt){
  if(FAILED(device_.As(&video_device_))||FAILED(context_.As(&video_context_)))return false;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
  content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputWidth=content.OutputWidth=w;content.InputHeight=content.OutputHeight=h;
  content.InputFrameRate={fps_,1};content.OutputFrameRate={fps_,1};
  content.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  if(FAILED(video_device_->CreateVideoProcessorEnumerator(&content,&video_enumerator_))||
     FAILED(video_device_->CreateVideoProcessor(video_enumerator_.Get(),0,&video_processor_)))return false;
  D3D11_TEXTURE2D_DESC input{};input.Width=w;input.Height=h;input.MipLevels=input.ArraySize=1;
  input.Format=fmt;input.SampleDesc.Count=1;input.Usage=D3D11_USAGE_DEFAULT;
  D3D11_TEXTURE2D_DESC output=input;output.Format=DXGI_FORMAT_NV12;output.BindFlags=D3D11_BIND_RENDER_TARGET;
  D3D11_TEXTURE2D_DESC staging=output;staging.BindFlags=0;staging.Usage=D3D11_USAGE_STAGING;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  for(unsigned i=0;i<kStagingSlots;i++){
    Slot s;
    if(FAILED(device_->CreateTexture2D(&input,nullptr,&s.input))||
       FAILED(device_->CreateTexture2D(&output,nullptr,&s.converted))||
       FAILED(device_->CreateTexture2D(&staging,nullptr,&s.staging)))return false;
    D3D11_QUERY_DESC query{};query.Query=D3D11_QUERY_EVENT;
    if(FAILED(device_->CreateQuery(&query,&s.source_copied)))return false;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};iv.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;iv.Texture2D.MipSlice=0;iv.Texture2D.ArraySlice=0;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{};ov.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;ov.Texture2D.MipSlice=0;
    if(FAILED(video_device_->CreateVideoProcessorInputView(s.input.Get(),video_enumerator_.Get(),&iv,&s.input_view))||
       FAILED(video_device_->CreateVideoProcessorOutputView(s.converted.Get(),video_enumerator_.Get(),&ov,&s.output_view)))return false;
    slots_.push_back(std::move(s));
  }
  video_context_->VideoProcessorSetStreamFrameFormat(video_processor_.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  pixel_format_="nv12";gpu_nv12_=true;
  if(w==4320&&h==2160){frame_.resize((size_t)3840*2160*3/2);extra_frame_.resize((size_t)960*1080*3/2);}
  else{frame_.resize((size_t)w*h*3/2);extra_frame_.clear();}
  debug("D3D11 GPU BGRA-to-NV12 conversion enabled");
  return true;
}
bool SvrtDirectMode::ConvertSlot(Slot &slot){
  D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=slot.input_view.Get();
  if(FAILED(video_context_->VideoProcessorBlt(video_processor_.Get(),slot.output_view.Get(),0,1,&stream)))return false;
  context_->CopyResource(slot.staging.Get(),slot.converted.Get());return true;
}
bool SvrtDirectMode::WaitForSourceCopy(Slot &slot){
  if(!slot.source_copied)return false;
  context_->End(slot.source_copied.Get());
  context_->Flush();
  const auto started=std::chrono::steady_clock::now();
  const unsigned capture_fps=width_==4320?std::min(fps_,kNativeStreamFps):fps_;
  const auto timeout=std::chrono::nanoseconds(1000000000ull/std::max(1u,capture_fps));
  unsigned polls=0;
  for(;;){
    const HRESULT ready=context_->GetData(slot.source_copied.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(ready==S_OK){
      const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
      if(elapsed>20)debugf("virtual display: source copy fence took %lldms",static_cast<long long>(elapsed));
      return true;
    }
    if(ready!=S_FALSE){debugf("virtual display: source copy fence failed: 0x%08lx",static_cast<unsigned long>(ready));return false;}
    if(std::chrono::steady_clock::now()-started>=timeout){
      debugf("virtual display: source copy fence timed out; device=0x%08lx",static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
      return false;
    }
    Sleep(polls++<4?0:1);
  }
}
bool SvrtDirectMode::EnsureSlots(unsigned ew,unsigned h,DXGI_FORMAT fmt){if(!slots_.empty())return true;width_=std::min(ew,1920u)*2;height_=std::min(h,2160u);debugf("preparing fixed stream: source=%ux%u output=%ux%u",ew,h,width_,height_);if(!EnsureGpuConversion(width_,height_,fmt)){debug("D3D11 NV12 conversion unavailable");slots_.clear();return false;}return StartEncoder(width_,height_);}
bool SvrtDirectMode::EnsureVirtualSlots(unsigned w,unsigned h,DXGI_FORMAT fmt){if(!slots_.empty())return true;width_=std::min(w,4320u);height_=std::min(h,2160u);debugf("preparing virtual-display stream: %ux%u",width_,height_);if(!EnsureGpuConversion(width_,height_,fmt)){debug("D3D11 NV12 conversion unavailable");slots_.clear();return false;}return StartEncoder(width_,height_);}
void SvrtDirectMode::Present(vr::SharedTextureHandle_t){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_) return;
  std::unique_lock<std::mutex> l(mutex_);auto a=textures_.find((uint64_t)submitted_[0]),b=textures_.find((uint64_t)submitted_[1]);if(a==textures_.end()||b==textures_.end())return;D3D11_TEXTURE2D_DESC d{};a->second.texture->GetDesc(&d);std::lock_guard<std::mutex> dl(d3d_mutex_);if(!EnsureSlots(d.Width,d.Height,d.Format))return;auto slot=std::find_if(slots_.begin(),slots_.end(),[](const Slot&s){return !s.pending;});if(slot==slots_.end())return;unsigned eye_width=width_/2,copy_width=std::min(d.Width,eye_width),copy_height=std::min(d.Height,height_),source_x=(d.Width-copy_width)/2,source_y=(d.Height-copy_height)/2;D3D11_BOX box{source_x,source_y,0,source_x+copy_width,source_y+copy_height,1};context_->CopySubresourceRegion(slot->input.Get(),0,0,0,0,a->second.texture.Get(),0,&box);context_->CopySubresourceRegion(slot->input.Get(),0,eye_width,0,0,b->second.texture.Get(),0,&box);if(!ConvertSlot(*slot))return;context_->Flush();slot->pending=true;slot->sequence=++sequence_;l.unlock();ready_.notify_one();}
void SvrtDirectMode::PresentVirtual(vr::SharedTextureHandle_t handle){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_||!device_||!handle||!receiver_available_||encoder_failed_)return;

  // The handle is only guaranteed to refer to the compositor backbuffer for
  // the duration of Present().  Deferring OpenSharedResource/CopyResource to
  // the encoder thread races SteamVR reusing that texture and produces a
  // perfectly valid stream of stale (usually grey) pixels.  Copy the GPU
  // resource while the compositor owns it, then let the worker perform the
  // asynchronous map/encode.
  if(CopyVirtualFrame(handle)) ready_.notify_one();
}
void SvrtDirectMode::WaitForVirtualPresent(){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_||!context_) return;
  // Present queues the GPU copy. Mapping the staging resource is deliberately
  // left to the encoder worker; this call only gives SteamVR the required
  // synchronization point without making its compositor thread wait for a
  // full-frame CPU readback.
  std::lock_guard<std::mutex> dl(d3d_mutex_);
  context_->Flush();
}
bool SvrtDirectMode::CopyVirtualFrame(vr::SharedTextureHandle_t handle){
  ComPtr<ID3D11Texture2D> source;
  std::unique_lock<std::mutex> l(mutex_);
  const auto now=std::chrono::steady_clock::now();
  const unsigned capture_fps=width_==4320?std::min(fps_,kNativeStreamFps):fps_;
  const auto interval=std::chrono::nanoseconds(1000000000ull/std::max(1u,capture_fps));
  if(next_capture_.time_since_epoch().count()&&now<next_capture_)return false;
  if(!next_capture_.time_since_epoch().count()||now-next_capture_>interval*2)next_capture_=now+interval;
  else next_capture_+=interval;
  if(FAILED(device_->OpenSharedResource(
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle)),
      IID_PPV_ARGS(&source)))||!source){
    debugf("virtual display: OpenSharedResource failed for handle=%p",
           reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
    return false;
  }

  ComPtr<IDXGIKeyedMutex> keyed;
  const bool has_keyed_mutex=SUCCEEDED(source.As(&keyed));
  if(has_keyed_mutex){
    const HRESULT acquired=keyed->AcquireSync(0,10);
    if(acquired!=S_OK){
      debugf("virtual display: AcquireSync failed: 0x%08lx",
             static_cast<unsigned long>(acquired));
      return false;
    }
  }

  bool copied=false;
  {
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    if(d.Width!=logged_virtual_width_||d.Height!=logged_virtual_height_||
       d.Format!=logged_virtual_format_){
      debugf("virtual display source: %ux%u format=%u samples=%u array=%u mips=%u misc=0x%08x",
             d.Width,d.Height,static_cast<unsigned>(d.Format),d.SampleDesc.Count,
             d.ArraySize,d.MipLevels,d.MiscFlags);
      logged_virtual_width_=d.Width;
      logged_virtual_height_=d.Height;
      logged_virtual_format_=d.Format;
    }
    if(d.SampleDesc.Count==1 && EnsureVirtualSlots(d.Width,d.Height,d.Format)){
      auto slot=std::find_if(slots_.begin(),slots_.end(),
                             [](const Slot&s){return !s.pending;});
      if(slot!=slots_.end()){
        // CopyResource has no HRESULT; a device-removal check catches the
        // only asynchronous failure that can invalidate the copy.
        context_->CopyResource(slot->input.Get(),source.Get());
        if(!WaitForSourceCopy(*slot)){
          if(has_keyed_mutex) keyed->ReleaseSync(0);
          return false;
        }
        if(!ConvertSlot(*slot)){
          if(has_keyed_mutex) keyed->ReleaseSync(0);
          return false;
        }
        context_->Flush();
        const HRESULT device_status=device_->GetDeviceRemovedReason();
        if(SUCCEEDED(device_status)){
          slot->pending=true;
          slot->sequence=++sequence_;
          copied=true;
        }else{
          debugf("virtual display: CopyResource device error: 0x%08lx",
                 static_cast<unsigned long>(device_status));
        }
      }
    }else{
      debug("virtual display: unsupported source texture description");
    }
  }
  if(has_keyed_mutex) keyed->ReleaseSync(0);
  return copied;
}
static bool valid_encoder(const std::string &encoder){return encoder=="hevc_nvenc"||encoder=="hevc_qsv"||encoder=="hevc_amf"||encoder=="libx265"||encoder=="hevc_v4l2request";}
struct CompanionEncoder {
  const char *codec;
  const char *low_latency_options;
};
static CompanionEncoder companion_encoder(const std::string &encoder){
  if(encoder=="hevc_nvenc")return {"h264_nvenc","-preset p1 -tune ull -zerolatency 1 -delay 0 -rc-lookahead 0 -rc cbr"};
  if(encoder=="hevc_qsv")return {"h264_qsv","-preset veryfast -low_delay_brc 1 -look_ahead 0"};
  if(encoder=="hevc_amf")return {"h264_amf","-usage ultralowlatency -quality speed -rc cbr"};
  /* Software HEVC, or a backend without a matching H.264 encoder, uses the
     portable software fallback rather than assuming NVIDIA hardware. */
  return {"libx264","-preset ultrafast -tune zerolatency"};
}
static bool valid_host(const std::string &host){if(host.empty()||host.size()>253||host.find('"')!=std::string::npos||host.find(' ')!=std::string::npos)return false;if(host.find(':')!=std::string::npos){int groups=0;size_t group_size=0;bool compressed=false;bool digit=false;for(size_t i=0;i<host.size();++i){const char ch=host[i];if(ch==':'){if(i+1<host.size()&&host[i+1]==':'){if(compressed)return false;compressed=true;++i;group_size=0;continue;}if(!group_size)return false;++groups;group_size=0;continue;}if(std::isxdigit(static_cast<unsigned char>(ch))){if(++group_size>4)return false;digit=true;continue;}if(ch=='.')continue;return false;}if(group_size)++groups;return digit&&compressed?groups<=8:(groups==8);}bool label_start=true;for(char ch:host){if(std::isalnum(static_cast<unsigned char>(ch))||ch=='_'){label_start=false;continue;}if(ch=='.'||ch=='-'){if(label_start)return false;label_start=true;continue;}return false;}return !label_start;}
bool SvrtDirectMode::StartEncoder(unsigned w,unsigned h){
    if(pipe_!=INVALID_HANDLE_VALUE)return true;
    if(w>4320){debugf("refusing %ux%u stream: maximum packed width is 4320",w,h);encoder_failed_=true;return false;}
    if(!valid_encoder(encoder_)||!valid_host(host_)||ffmpeg_.empty()||ffmpeg_.find('"')!=std::string::npos){debug("refusing unsafe FFmpeg command values");encoder_failed_=true;return false;}
    constexpr unsigned kCompanionPortOffset=3;
    const bool native=w==4320&&h==2160;
    if(native&&port_>std::numeric_limits<uint16_t>::max()-kCompanionPortOffset){debugf("refusing native stream: video port %u cannot add companion offset %u",port_,kCompanionPortOffset);encoder_failed_=true;return false;}
    std::string endpoint=host_.find(':')==std::string::npos?host_:"["+host_+"]";
    auto launch=[&](const char *cmd,HANDLE &write_pipe,HANDLE &process,DWORD &pid)->bool{
      SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read_pipe=nullptr;
      if(!CreatePipe(&read_pipe,&write_pipe,&sa,1<<20))return false;
      SetHandleInformation(write_pipe,HANDLE_FLAG_INHERIT,0);STARTUPINFOA si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=read_pipe;si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
      PROCESS_INFORMATION pi{};BOOL ok=CreateProcessA(nullptr,const_cast<char*>(cmd),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);CloseHandle(read_pipe);
      if(!ok){CloseHandle(write_pipe);write_pipe=INVALID_HANDLE_VALUE;return false;}
      CloseHandle(pi.hThread);process=pi.hProcess;pid=pi.dwProcessId;return true;
    };
    char main_cmd[4096];unsigned main_w=w==4320?3840:w,encode_fps=w==4320?std::min(fps_,kNativeStreamFps):fps_,keyint=w==4320?10:fps_;
    int command_size=std::snprintf(main_cmd,sizeof(main_cmd),
      "\"%s\" -hide_banner -loglevel warning -rw_timeout 500000 -fflags nobuffer -flags low_delay "
      "-f rawvideo -pix_fmt %s -video_size %ux%u -framerate %u -i pipe:0 -an -c:v %s -preset p1 -tune ull "
      "-zerolatency 1 -delay 0 -rc-lookahead 0 -rc cbr -b:v %uM -maxrate %uM -bufsize %uM "
      "-g %u -bf 0 -muxdelay 0 -muxpreload 0 -flush_packets 1 -f mpegts \"tcp://%s:%u?tcp_nodelay=1&send_buffer_size=65536\"",
      ffmpeg_.c_str(),pixel_format_.c_str(),main_w,h,encode_fps,encoder_.c_str(),bitrate_,bitrate_,
      std::max(1u,bitrate_/12),keyint,endpoint.c_str(),port_);
    if(command_size<0||static_cast<size_t>(command_size)>=sizeof(main_cmd)){debug("refusing truncated FFmpeg command");encoder_failed_=true;return false;}
    DWORD main_pid=0;if(!launch(main_cmd,pipe_,process_,main_pid)){encoder_failed_=true;debugf("failed to launch main FFmpeg: error %lu",GetLastError());return false;}
    DWORD extra_pid=0;
    if(native){
      const unsigned extra_bitrate=std::max(2u,bitrate_/8),extra_port=static_cast<unsigned>(port_)+kCompanionPortOffset;char extra_cmd[4096];
      const CompanionEncoder extra_encoder=companion_encoder(encoder_);
      command_size=std::snprintf(extra_cmd,sizeof(extra_cmd),
        "\"%s\" -hide_banner -loglevel warning -rw_timeout 500000 -fflags nobuffer -flags low_delay "
        "-f rawvideo -pix_fmt nv12 -video_size 960x1080 -framerate %u -i pipe:0 -an -c:v %s %s "
        "-b:v %uM -maxrate %uM "
        "-bufsize 1M -g %u -bf 0 -muxdelay 0 -muxpreload 0 -flush_packets 1 -f mpegts \"tcp://%s:%u?tcp_nodelay=1&send_buffer_size=65536\"",
        ffmpeg_.c_str(),encode_fps,extra_encoder.codec,extra_encoder.low_latency_options,
        extra_bitrate,extra_bitrate,keyint,endpoint.c_str(),extra_port);
      if(command_size<0||static_cast<size_t>(command_size)>=sizeof(extra_cmd)||!launch(extra_cmd,extra_pipe_,extra_process_,extra_pid)){
        debugf("failed to launch extra FFmpeg: error %lu",GetLastError());CloseEncoder();encoder_failed_=true;return false;
      }
    }
    encoder_failed_=false;debugf("FFmpeg started: main_pid=%lu stream=%ux%u extra_pid=%lu",main_pid,main_w,h,extra_pid);return true;
}
void SvrtDirectMode::CloseEncoder(){if(pipe_!=INVALID_HANDLE_VALUE){CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;}if(extra_pipe_!=INVALID_HANDLE_VALUE){CloseHandle(extra_pipe_);extra_pipe_=INVALID_HANDLE_VALUE;}if(process_){WaitForSingleObject(process_,2000);CloseHandle(process_);process_=nullptr;}if(extra_process_){WaitForSingleObject(extra_process_,2000);CloseHandle(extra_process_);extra_process_=nullptr;}}
void SvrtDirectMode::EncoderThread(){uint64_t transmitted=0;while(running_){std::unique_lock<std::mutex> l(mutex_);ready_.wait(l,[this]{return !running_||disconnect_requested_||std::any_of(slots_.begin(),slots_.end(),[](const Slot&s){return s.pending;});});if(!running_)break;if(disconnect_requested_.exchange(false)||!receiver_available_){for(auto &slot:slots_)slot.pending=false;next_capture_={};l.unlock();CloseEncoder();l.lock();slots_.clear();encoder_failed_=false;continue;}auto it=std::max_element(slots_.begin(),slots_.end(),[](const Slot&a,const Slot&b){return (!a.pending?0:a.sequence)<(!b.pending?0:b.sequence);});if(it==slots_.end()||!it->pending)continue;Slot *slot=&*it;for(auto &queued:slots_)if(&queued!=slot&&queued.pending&&queued.sequence<slot->sequence)queued.pending=false;unsigned w=width_,h=height_;uint64_t frame=slot->sequence;l.unlock();D3D11_MAPPED_SUBRESOURCE map{};bool ok=false;bool gpu_busy=false;const auto started=std::chrono::steady_clock::now();
  // The immediate context must be serialized, but the CPU copy itself does
  // not.  Keeping this lock during a full stereo-frame memcpy made Present()
  // wait behind the encoder worker and produced visible compositor hitches.
  {
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    if((process_&&WaitForSingleObject(process_,0)==WAIT_OBJECT_0)||(extra_process_&&WaitForSingleObject(extra_process_,0)==WAIT_OBJECT_0))encoder_failed_=true;
    const HRESULT mapped=encoder_failed_?E_FAIL:context_->Map(slot->staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
    gpu_busy=mapped==DXGI_ERROR_WAS_STILL_DRAWING;
    ok=SUCCEEDED(mapped);
  }
  if(gpu_busy){Sleep(1);continue;}
  auto mapped_at=std::chrono::steady_clock::now();if(ok){
    if(gpu_nv12_&&w==4320&&h==2160){
      const auto *source=static_cast<const uint8_t*>(map.pData);
      constexpr unsigned main_w=3840,extra_w=960,half_h=1080;
      for(unsigned y=0;y<h;y++)std::memcpy(frame_.data()+(size_t)y*main_w,source+(size_t)y*map.RowPitch,main_w);
      auto *extra_y=extra_frame_.data();
      for(unsigned y=0;y<half_h;y++){
        std::memcpy(extra_y+(size_t)y*extra_w,source+(size_t)y*map.RowPitch+main_w,480);
        std::memcpy(extra_y+(size_t)y*extra_w+480,source+(size_t)(y+half_h)*map.RowPitch+main_w,480);
      }
      const auto *uv=source+(size_t)map.RowPitch*h;
      auto *main_uv=frame_.data()+(size_t)main_w*h;
      for(unsigned y=0;y<h/2;y++)std::memcpy(main_uv+(size_t)y*main_w,uv+(size_t)y*map.RowPitch,main_w);
      auto *extra_uv=extra_y+(size_t)extra_w*half_h;
      for(unsigned y=0;y<half_h/2;y++){
        std::memcpy(extra_uv+(size_t)y*extra_w,uv+(size_t)y*map.RowPitch+main_w,480);
        std::memcpy(extra_uv+(size_t)y*extra_w+480,uv+(size_t)(y+half_h/2)*map.RowPitch+main_w,480);
      }
    }else{
      const auto *source=static_cast<const uint8_t*>(map.pData);
      for(unsigned y=0;y<h;y++)std::memcpy(frame_.data()+(size_t)y*w,source+(size_t)y*map.RowPitch,w);
      const auto *uv=source+(size_t)map.RowPitch*h;auto *uv_out=frame_.data()+(size_t)w*h;
      for(unsigned y=0;y<h/2;y++)std::memcpy(uv_out+(size_t)y*w,uv+(size_t)y*map.RowPitch,w);
    }
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    context_->Unmap(slot->staging.Get(),0);
  }
  auto copied_at=std::chrono::steady_clock::now();
  auto write_frame=[&](HANDLE target,const std::vector<uint8_t>&bytes){const uint8_t *data=bytes.data();size_t remaining=bytes.size();while(remaining&&running_){DWORD n=0;const DWORD chunk=(DWORD)std::min<size_t>(remaining,0x7fffffffu);if(!WriteFile(target,data,chunk,&n,nullptr)||!n)return false;data+=n;remaining-=n;}return remaining==0;};
  /* Main-first ordering makes the HEVC pipe the flow-control clock.  The
     companion decoder starts much faster; writing it first lets H.264 run
     dozens of frame IDs ahead while rpivid is still initializing. */
  HANDLE pipe=pipe_;if(ok&&pipe!=INVALID_HANDLE_VALUE)ok=write_frame(pipe,frame_);
  const auto main_written_at=std::chrono::steady_clock::now();
  if(ok&&!extra_frame_.empty()){HANDLE extra_pipe=extra_pipe_;ok=extra_pipe!=INVALID_HANDLE_VALUE&&write_frame(extra_pipe,extra_frame_);}
  const auto extra_written_at=std::chrono::steady_clock::now();
  l.lock();slot->pending=false;
  if(ok){transmitted++;if(transmitted==1||transmitted%fps_==0){const auto map_us=std::chrono::duration_cast<std::chrono::microseconds>(mapped_at-started).count(),copy_us=std::chrono::duration_cast<std::chrono::microseconds>(copied_at-mapped_at).count(),main_us=std::chrono::duration_cast<std::chrono::microseconds>(main_written_at-copied_at).count(),extra_us=std::chrono::duration_cast<std::chrono::microseconds>(extra_written_at-main_written_at).count();debugf("transmitted frame=%llu source_sequence=%llu size=%ux%u map=%.2fms copy=%.2fms main_pipe=%.2fms extra_pipe=%.2fms",(unsigned long long)transmitted,(unsigned long long)frame,w,h,map_us/1000.0,copy_us/1000.0,main_us/1000.0,extra_us/1000.0);}}
  if(!ok&&running_&&!gpu_busy){const bool first_failure=!encoder_failed_.exchange(true);l.unlock();if(first_failure)debug("encoder pipe write failed; pausing before retry");CloseEncoder();l.lock();slots_.clear();l.unlock();Sleep(1000);if(running_&&receiver_available_)encoder_failed_=false;l.lock();}}}
