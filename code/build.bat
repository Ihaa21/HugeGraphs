@echo off

set CodeDir=..\code
set DataDir=..\data
set LibsDir=..\libs
set OutputDir=..\build_win32
set VulkanIncludeDir="C:\VulkanSDK\1.2.135.0\Include\vulkan"
set VulkanBinDir="C:\VulkanSDK\1.2.135.0\Bin"
set AssimpDir=%LibsDir%\framework_vulkan

set CommonCompilerFlags=-Od -MTd -nologo -fp:fast -fp:except- -EHsc -Gm- -GR- -EHa- -Zo -Oi -WX -W4 -wd4127 -wd4201 -wd4100 -wd4189 -wd4505 -Z7 -FC
set CommonCompilerFlags=-I %VulkanIncludeDir% %CommonCompilerFlags%
set CommonCompilerFlags=-I %LibsDir% -I %AssimpDir% %CommonCompilerFlags%
REM Check the DLLs here
set CommonLinkerFlags=-incremental:no -opt:ref user32.lib gdi32.lib Winmm.lib opengl32.lib DbgHelp.lib d3d12.lib dxgi.lib d3dcompiler.lib %AssimpDir%\assimp\libs\assimp-vc142-mt.lib

IF NOT EXIST %OutputDir% mkdir %OutputDir%

pushd %OutputDir%

del *.pdb > NUL 2> NUL

REM USING GLSL IN VK USING GLSLANGVALIDATOR
call glslangValidator -DCIRCLE_VERTEX_SHADER=1 -S vert -e main -g -V -o %DataDir%\shader_circle_vert.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DCIRCLE_FRAGMENT_SHADER=1 -S frag -e main -g -V -o %DataDir%\shader_circle_frag.spv %CodeDir%\graph_shaders.cpp

call glslangValidator -DLINE_VERTEX_SHADER=1 -S vert -e main -g -V -o %DataDir%\shader_line_vert.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DLINE_2_VERTEX_SHADER=1 -S vert -e main -g -V -o %DataDir%\shader_line_2_vert.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DLINE_FRAGMENT_SHADER=1 -S frag -e main -g -V -o %DataDir%\shader_line_frag.spv %CodeDir%\graph_shaders.cpp

call glslangValidator -DGRAPH_MOVE_CONNECTIONS=1 -S comp -e main -g -V -o %DataDir%\shader_graph_move_connections.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DGRAPH_REPULSION=1 -S comp -e main -g -V -o %DataDir%\shader_graph_repulsion.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DGRAPH_CALC_GLOBAL_SPEED=1 -S comp --target-env spirv1.3 -e main -g -V -o %DataDir%\shader_graph_calc_global_speed.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DGRAPH_UPDATE_NODES=1 -S comp -e main -g -V -o %DataDir%\shader_graph_update_nodes.spv %CodeDir%\graph_shaders.cpp

REM Sort Shaders
call glslangValidator -DBITONIC_GLOBAL_FLIP=1 -S comp -e main -g -V -o %DataDir%\shader_merge_global_flip.spv %CodeDir%\sort_shaders.cpp
call glslangValidator -DBITONIC_LOCAL_DISPERSE=1 -S comp -e main -g -V -o %DataDir%\shader_merge_local_disperse.spv %CodeDir%\sort_shaders.cpp
call glslangValidator -DBITONIC_GLOBAL_DISPERSE=1 -S comp -e main -g -V -o %DataDir%\shader_merge_global_disperse.spv %CodeDir%\sort_shaders.cpp
call glslangValidator -DBITONIC_LOCAL_FD=1 -S comp -e main -g -V -o %DataDir%\shader_merge_local_fd.spv %CodeDir%\sort_shaders.cpp
call glslangValidator -S comp -e main -g -V -o %DataDir%\shader_atomic_merge_sort.spv %CodeDir%\sort_shaders_atomic.cpp

REM RadixTree Shaders
call glslangValidator -DGENERATE_MORTON_KEYS=1 -S comp --target-env spirv1.3 -e main -g -V -o %DataDir%\shader_generate_morton_keys.spv %CodeDir%\radixtree_shaders.cpp
call glslangValidator -DCALC_WORLD_BOUNDS=1 -S comp --target-env spirv1.3 -e main -g -V -o %DataDir%\shader_calc_world_bounds.spv %CodeDir%\radixtree_shaders.cpp
call glslangValidator -DRADIX_TREE_BUILD=1 -S comp -e main -g -V -o %DataDir%\shader_radix_tree_build.spv %CodeDir%\radixtree_shaders.cpp
call glslangValidator -DRADIX_TREE_SUMMARIZE=1 -S comp -e main -g -V -o %DataDir%\shader_radix_tree_summarize.spv %CodeDir%\radixtree_shaders.cpp
call glslangValidator -DRADIX_TREE_REPULSION=1 -S comp --target-env spirv1.3 -e main -g -V -o %DataDir%\shader_radix_tree_repulsion.spv %CodeDir%\radixtree_shaders.cpp

REM Parallel Sort Shaders (HLSL in VK using DXC)
set DxcDir=D:\Tools\dxc_2020_10-22\bin\x64
%DxcDir%\dxc.exe -spirv -DPARALLEL_SORT_COUNT=1 -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Wno-for-redefinition -Fo %DataDir%\shader_parallel_sort_count.spv %CodeDir%\parallel_sort_shaders.cpp
%DxcDir%\dxc.exe -spirv -DPARALLEL_SORT_REDUCE=1 -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Wno-for-redefinition -Fo %DataDir%\shader_parallel_sort_reduce.spv %CodeDir%\parallel_sort_shaders.cpp
%DxcDir%\dxc.exe -spirv -DPARALLEL_SORT_SCAN=1 -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Wno-for-redefinition -Fo %DataDir%\shader_parallel_sort_scan.spv %CodeDir%\parallel_sort_shaders.cpp
%DxcDir%\dxc.exe -spirv -DPARALLEL_SORT_SCAN_ADD=1 -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Wno-for-redefinition -Fo %DataDir%\shader_parallel_sort_scan_add.spv %CodeDir%\parallel_sort_shaders.cpp
%DxcDir%\dxc.exe -spirv -DPARALLEL_SORT_SCATTER=1 -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Wno-for-redefinition -Fo %DataDir%\shader_parallel_sort_scatter.spv %CodeDir%\parallel_sort_shaders.cpp

call cl %CommonCompilerFlags% -Fepreprocess.exe %CodeDir%\preprocess.cpp -Fmpreprocess.map /link %CommonLinkerFlags%

REM 64-bit build
echo WAITING FOR PDB > lock.tmp
cl %CommonCompilerFlags% %CodeDir%\huge_graphs_demo.cpp -Fmhuge_graphs_demo.map -LD /link %CommonLinkerFlags% -incremental:no -opt:ref -PDB:huge_graphs_demo_%random%.pdb -EXPORT:Init -EXPORT:Destroy -EXPORT:SwapChainChange -EXPORT:CodeReload -EXPORT:MainLoop
del lock.tmp
call cl %CommonCompilerFlags% -DDLL_NAME=huge_graphs_demo -Fehuge_graphs_demo.exe %LibsDir%\framework_vulkan\win32_main.cpp -Fmhuge_graphs_demo.map /link %CommonLinkerFlags%

popd
