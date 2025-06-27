%echo off

pushd %~dp0

set cauldron_dxc=..\..\DirectXCompiler\bin\x64\dxc.exe -T cs_6_2

if not exist "PrecompiledShadersDXIL" mkdir "PrecompiledShadersDXIL"

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOClearLoadCounter.h -Vn CSClearLoadCounterDXIL -E FFX_CACAO_ClearLoadCounter ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledDepths.h                  -Vn CSPrepareDownsampledDepthsDXIL                  -E FFX_CACAO_PrepareDownsampledDepths                  ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeDepths.h                       -Vn CSPrepareNativeDepthsDXIL                       -E FFX_CACAO_PrepareNativeDepths                       ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledDepthsAndMips.h           -Vn CSPrepareDownsampledDepthsAndMipsDXIL            -E FFX_CACAO_PrepareDownsampledDepthsAndMips           ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeDepthsAndMips.h                -Vn CSPrepareNativeDepthsAndMipsDXIL                 -E FFX_CACAO_PrepareNativeDepthsAndMips                ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledNormals.h                 -Vn CSPrepareDownsampledNormalsDXIL                  -E FFX_CACAO_PrepareDownsampledNormals                 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeNormals.h                      -Vn CSPrepareNativeNormalsDXIL                       -E FFX_CACAO_PrepareNativeNormals                      ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledNormalsFromInputNormals.h -Vn CSPrepareDownsampledNormalsFromInputNormalsDXIL  -E FFX_CACAO_PrepareDownsampledNormalsFromInputNormals ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeNormalsFromInputNormals.h      -Vn CSPrepareNativeNormalsFromInputNormalsDXIL       -E FFX_CACAO_PrepareNativeNormalsFromInputNormals      ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareDownsampledDepthsHalf.h              -Vn CSPrepareDownsampledDepthsHalfDXIL               -E FFX_CACAO_PrepareDownsampledDepthsHalf              ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPrepareNativeDepthsHalf.h                   -Vn CSPrepareNativeDepthsHalfDXIL                    -E FFX_CACAO_PrepareNativeDepthsHalf                   ffx_cacao.hlsl


%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOGenerateQ0.h     -Vn CSGenerateQ0DXIL      -E FFX_CACAO_GenerateQ0     ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOGenerateQ1.h     -Vn CSGenerateQ1DXIL      -E FFX_CACAO_GenerateQ1     ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOGenerateQ2.h     -Vn CSGenerateQ2DXIL      -E FFX_CACAO_GenerateQ2     ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOGenerateQ3.h     -Vn CSGenerateQ3DXIL      -E FFX_CACAO_GenerateQ3     ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOGenerateQ3Base.h -Vn CSGenerateQ3BaseDXIL  -E FFX_CACAO_GenerateQ3Base ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOGenerateImportanceMap.h     -Vn CSGenerateImportanceMapDXIL      -E FFX_CACAO_GenerateImportanceMap     ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPostprocessImportanceMapA.h -Vn CSPostprocessImportanceMapADXIL  -E FFX_CACAO_PostprocessImportanceMapA ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOPostprocessImportanceMapB.h -Vn CSPostprocessImportanceMapBDXIL  -E FFX_CACAO_PostprocessImportanceMapB ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur1.h -Vn CSEdgeSensitiveBlur1DXIL  -E FFX_CACAO_EdgeSensitiveBlur1 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur2.h -Vn CSEdgeSensitiveBlur2DXIL  -E FFX_CACAO_EdgeSensitiveBlur2 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur3.h -Vn CSEdgeSensitiveBlur3DXIL  -E FFX_CACAO_EdgeSensitiveBlur3 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur4.h -Vn CSEdgeSensitiveBlur4DXIL  -E FFX_CACAO_EdgeSensitiveBlur4 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur5.h -Vn CSEdgeSensitiveBlur5DXIL  -E FFX_CACAO_EdgeSensitiveBlur5 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur6.h -Vn CSEdgeSensitiveBlur6DXIL  -E FFX_CACAO_EdgeSensitiveBlur6 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur7.h -Vn CSEdgeSensitiveBlur7DXIL  -E FFX_CACAO_EdgeSensitiveBlur7 ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOEdgeSensitiveBlur8.h -Vn CSEdgeSensitiveBlur8DXIL  -E FFX_CACAO_EdgeSensitiveBlur8 ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOApply.h             -Vn CSApplyDXIL              -E FFX_CACAO_Apply             ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAONonSmartApply.h     -Vn CSNonSmartApplyDXIL      -E FFX_CACAO_NonSmartApply     ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAONonSmartHalfApply.h -Vn CSNonSmartHalfApplyDXIL  -E FFX_CACAO_NonSmartHalfApply ffx_cacao.hlsl

%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOUpscaleBilateral5x5NonSmart.h -Vn CSUpscaleBilateral5x5NonSmartDXIL -E FFX_CACAO_UpscaleBilateral5x5NonSmart ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOUpscaleBilateral5x5Smart.h    -Vn CSUpscaleBilateral5x5SmartDXIL    -E FFX_CACAO_UpscaleBilateral5x5Smart    ffx_cacao.hlsl
%cauldron_dxc% -Fh PrecompiledShadersDXIL/CACAOUpscaleBilateral5x5Half.h     -Vn CSUpscaleBilateral5x5HalfDXIL     -E FFX_CACAO_UpscaleBilateral5x5Half     ffx_cacao.hlsl

popd
