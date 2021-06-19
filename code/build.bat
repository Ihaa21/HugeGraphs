@echo off

set CodeDir=..\code
set DataDir=..\data
set LibsDir=..\libs
set OutputDir=..\build_win32
set VulkanIncludeDir="C:\VulkanSDK\1.2.135.0\Include\vulkan"
set VulkanBinDir="C:\VulkanSDK\1.2.135.0\Bin"
set AssimpDir=%LibsDir%\framework_vulkan

set CommonCompilerFlags=-O2 -MTd -nologo -fp:fast -fp:except- -EHsc -Gm- -GR- -EHa- -Zo -Oi -WX -W4 -wd4127 -wd4201 -wd4100 -wd4189 -wd4505 -Z7 -FC
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
call glslangValidator -DLINE_FRAGMENT_SHADER=1 -S frag -e main -g -V -o %DataDir%\shader_line_frag.spv %CodeDir%\graph_shaders.cpp

call glslangValidator -DGRAPH_MOVE_CONNECTIONS=1 -S comp -e main -g -V -o %DataDir%\shader_graph_move_connections.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DGRAPH_NEARBY=1 -S comp -e main -g -V -o %DataDir%\shader_graph_nearby.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DGRAPH_UPDATE_NODES=1 -S comp -e main -g -V -o %DataDir%\shader_graph_update_nodes.spv %CodeDir%\graph_shaders.cpp
call glslangValidator -DGRAPH_GEN_EDGES=1 -S comp -e main -g -V -o %DataDir%\shader_graph_gen_edges.spv %CodeDir%\graph_shaders.cpp

REM USING HLSL IN VK USING DXC
REM set DxcDir=C:\Tools\DirectXShaderCompiler\build\Debug\bin
REM %DxcDir%\dxc.exe -spirv -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Fo ..\data\write_cs.o -Fh ..\data\write_cs.o.txt ..\code\bw_write_shader.cpp

REM 64-bit build
echo WAITING FOR PDB > lock.tmp
cl %CommonCompilerFlags% %CodeDir%\huge_graphs_demo.cpp -Fmhuge_graphs_demo.map -LD /link %CommonLinkerFlags% -incremental:no -opt:ref -PDB:huge_graphs_demo_%random%.pdb -EXPORT:Init -EXPORT:Destroy -EXPORT:SwapChainChange -EXPORT:CodeReload -EXPORT:MainLoop
del lock.tmp
call cl %CommonCompilerFlags% -DDLL_NAME=huge_graphs_demo -Fehuge_graphs_demo.exe %LibsDir%\framework_vulkan\win32_main.cpp -Fmhuge_graphs_demo.map /link %CommonLinkerFlags%

popd
