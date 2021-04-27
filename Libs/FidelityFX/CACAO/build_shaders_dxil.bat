%echo off

pushd %~dp0

set PATH_DXC=..\..\DirectXCompiler\bin\x64\dxc.exe -T cs_6_2

if not exist "PrecompiledShadersDXIL" mkdir "PrecompiledShadersDXIL"

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledDepths.h                  -Vn CSPrepareDownsampledDepthsDXIL                  -E CSPrepareDownsampledDepths                  ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeDepths.h                       -Vn CSPrepareNativeDepthsDXIL                       -E CSPrepareNativeDepths                       ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledDepthsAndMips.h           -Vn CSPrepareDownsampledDepthsAndMipsDXIL            -E CSPrepareDownsampledDepthsAndMips           ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeDepthsAndMips.h                -Vn CSPrepareNativeDepthsAndMipsDXIL                 -E CSPrepareNativeDepthsAndMips                ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledNormals.h                 -Vn CSPrepareDownsampledNormalsDXIL                  -E CSPrepareDownsampledNormals                 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeNormals.h                      -Vn CSPrepareNativeNormalsDXIL                       -E CSPrepareNativeNormals                      ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledNormalsFromInputNormals.h -Vn CSPrepareDownsampledNormalsFromInputNormalsDXIL  -E CSPrepareDownsampledNormalsFromInputNormals ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeNormalsFromInputNormals.h      -Vn CSPrepareNativeNormalsFromInputNormalsDXIL       -E CSPrepareNativeNormalsFromInputNormals      ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledDepthsHalf.h              -Vn CSPrepareDownsampledDepthsHalfDXIL               -E CSPrepareDownsampledDepthsHalf              ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeDepthsHalf.h                   -Vn CSPrepareNativeDepthsHalfDXIL                    -E CSPrepareNativeDepthsHalf                   ffx_cacao.hlsl


%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOGenerateQ0.h     -Vn CSGenerateQ0DXIL      -E CSGenerateQ0     ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOGenerateQ1.h     -Vn CSGenerateQ1DXIL      -E CSGenerateQ1     ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOGenerateQ2.h     -Vn CSGenerateQ2DXIL      -E CSGenerateQ2     ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOGenerateQ3.h     -Vn CSGenerateQ3DXIL      -E CSGenerateQ3     ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOGenerateQ3Base.h -Vn CSGenerateQ3BaseDXIL  -E CSGenerateQ3Base ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOGenerateImportanceMap.h     -Vn CSGenerateImportanceMapDXIL      -E CSGenerateImportanceMap     ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPostprocessImportanceMapA.h -Vn CSPostprocessImportanceMapADXIL  -E CSPostprocessImportanceMapA ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOPostprocessImportanceMapB.h -Vn CSPostprocessImportanceMapBDXIL  -E CSPostprocessImportanceMapB ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur1.h -Vn CSEdgeSensitiveBlur1DXIL  -E CSEdgeSensitiveBlur1 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur2.h -Vn CSEdgeSensitiveBlur2DXIL  -E CSEdgeSensitiveBlur2 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur3.h -Vn CSEdgeSensitiveBlur3DXIL  -E CSEdgeSensitiveBlur3 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur4.h -Vn CSEdgeSensitiveBlur4DXIL  -E CSEdgeSensitiveBlur4 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur5.h -Vn CSEdgeSensitiveBlur5DXIL  -E CSEdgeSensitiveBlur5 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur6.h -Vn CSEdgeSensitiveBlur6DXIL  -E CSEdgeSensitiveBlur6 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur7.h -Vn CSEdgeSensitiveBlur7DXIL  -E CSEdgeSensitiveBlur7 ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur8.h -Vn CSEdgeSensitiveBlur8DXIL  -E CSEdgeSensitiveBlur8 ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOApply.h             -Vn CSApplyDXIL              -E CSApply             ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAONonSmartApply.h     -Vn CSNonSmartApplyDXIL      -E CSNonSmartApply     ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAONonSmartHalfApply.h -Vn CSNonSmartHalfApplyDXIL  -E CSNonSmartHalfApply ffx_cacao.hlsl

%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOUpscaleBilateral5x5.h        -Vn CSUpscaleBilateral5x5DXIL         -E CSUpscaleBilateral5x5        ffx_cacao.hlsl
%PATH_DXC% -Fh PrecompiledShadersDXIL/CACAOUpscaleBilateral5x5Half.h    -Vn CSUpscaleBilateral5x5HalfDXIL     -E CSUpscaleBilateral5x5Half    ffx_cacao.hlsl

popd
