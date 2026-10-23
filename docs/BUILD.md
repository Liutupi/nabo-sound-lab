# 构建与资源

无线对战预览版v0.2.0-preview.1使用冻结0.2.0-dev镜像。构建入口为 `firmware/tools/build-duel.ps1 -Action all`；使用独立build-duel与sdkconfig.duel，完整已测配置和默认配置均随源码提供。普通build.ps1不启用无线菜单。依赖路径可通过脚本参数设置为自己的ESP-IDF/工具及组件位置。宿主测试依赖配置见tests/duel_host/README.md。重编结果需单独验证，不能替换Release已测镜像的身份。

发布包是冻结的v0.1.2 r4镜像；本次发布没有重新编译。自己的构建可能因工具链、时间及配置不同产生不同SHA，不代表公开镜像已被重新实测。

依赖：ESP-IDF5.5.3、ESP32-C3工具链；button4.2.0、esp_codec_dev1.6.2，版本见firmware/components/bsp/idf_component.yml。Windows脚本firmware/tools/build.ps1接受-IdfPath、-VendorDir、-ToolsPath；默认路径来自作者本机缓存，需要改成自己的环境。VendorDir中需具备对应版本的button和esp_codec_dev及其依赖。

仓库保留firmware/assets/soundlab_assets.bin和全部运行时素材；不重画、不重生成字体即可使用冻结素材构建。素材文件SHA和发布二进制见验证JSON及Release manifest。重新生成素材需Python、Pillow、fontTools、NumPy，视频抽帧另需FFmpeg。

```powershell
python -m pip install Pillow fonttools numpy
# 仅在确实修改素材/字形后运行，会改变素材输出：
python tools/gen_fonts_soundlab.py
python tools/pack_props_soundlab.py
python tools/pack_assets_soundlab.py
# 使用自己的已安装环境路径：
& .irmware	oolsuild.ps1 -Action all -IdfPath <idf目录> -VendorDir <组件目录> -ToolsPath <IDF工具目录>
```

宿主测试在tests/。其Windows入口沿用开发期间TCC/Node路径，需安装编译器与Node并调整本机工具路径后运行；未随仓库分发宿主可执行工具或缓存。生成信号由tests/mic_host/gen_signals.py等脚本产生；本仓库不含环境拾音。

浏览器原型可在项目根目录python -m http.server 8000后打开prototype/index.html；麦克风功能需要浏览器权限。原型不是已刷镜像的替代验收，新增硬件音量UI尚未同步到旧原型。

NABO定稿以game-art/soundlab/costume/final.png和props/为准。sprites/是当前实际运行的索引色资源。源视频保留供动作复核，生成服务任务记录、签名URL、备份和设备日志不公开。
