# 独立着色器编译组件

本目录从 shadPS4 的固定提交 `529ceb32f5546b820588f194b903cc39d08015fe` 提取 GCN 解码、控制流、IR、优化和 SPIR-V 生成，以及所需 AMD 寄存器描述与基础头文件。原始文件名、散列和来源保存在 `upstream-files.json`；原作者声明保留在文件中，GPL-2.0-or-later 全文位于仓库 `LICENSES`。

它不提供 ELF 装载、客体线程、系统服务、应用设置、窗口或 Vulkan 呈现，也不链接完整模拟器核心。上层由 `src/graphics/shader_compiler.h` 提供不暴露上游类型的接口。

## 宿主边界

- `host/common` 将断言失败转为宿主 C++ 异常，日志写入 stderr，不与运行时的 JSON 日志混合。
- `host/host_srt.cpp` 与 `ketsui_gcn_host.h` 用显式 reader 访问常量内存，不生成 x86 遍历程序，不注册模拟器异常处理器，也不把非法读取修补为零。
- IR dump 不拥有模拟器用户目录。诊断导出由外部运行时负责。
- 直接内存访问/BDA 模式关闭；标量间接读取使用平坦常量缓冲，普通 constant-buffer load 仍编译为 GPU 缓冲读取。
- 解码切片检查每次读取的边界，着色器声明由上层有界解析；输出必须通过 SPIRV-Tools 的 Vulkan 1.2 校验。
- 直线型 fetch shader 经同一 reader 逐条读取，最多 4096 字节，核对返回 link register 后内联为普通缓冲 IR；不直接跟随客体代码指针，也不使用上游设置或 Vulkan 顶点输入表。
- Vulkan 1.2 的 discard 输出显式声明 `SPV_EXT_demote_to_helper_invocation`；上层设备必须实际启用相应 feature 和扩展。

原 `ir/program.cpp` 和 JIT 型 `flatten_extended_userdata_pass.cpp` 已由上述宿主实现替换。上游会在没有启用用户裁剪平面时仍分配一份平面缓冲，本地将空掩码检查提前，避免无效描述符。

上游 Liverpool/PM4 执行器及未使用的 AMD 实现文件不保留在此组件中；需要的寄存器和格式定义单独保留。来源清单的 excluded_files 记录排除项与原始散列，便于以后按需核对，不把未接入代码混入构建。

## 当前覆盖

当前接口支持顶点、像素阶段和有界直线型 fetch 内联。带分支或递归调用的 fetch、嵌套或动态 SRT 表达式、通过 constant-buffer 获取另一份 descriptor、clip/misc 输出映射及特殊缓冲绑定仍明确报告未实现。缓冲、图像和采样器描述符以复制后的数据交给上层，不暴露编译器内部生命周期。这里的覆盖不是完整 GPU 后端，编译成功不会生成 flip 完成事件。

`dependencies.json` 固定 fmt、magic_enum、Boost、Sirit、SPIR-V 声明和校验器的提交。SPIRV-Tools 的配套声明与 Sirit 的声明分别固定，避免升级校验器时改变编译器的输入头文件。`Prepare-NativeDeps.ps1` 准备 `.deps`，已有目录有修改或来源不符时保留原文件并报告；`Build-Native.ps1` 校验提交和实际源码散列。
