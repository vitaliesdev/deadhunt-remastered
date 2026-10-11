#define A_GPU 1
#define A_HLSL 1
#include "cas/ffx_a.h"
sampler2D sSrc : register(s0);
AF3 CasLoad(ASU2 p) { return tex2Dlod(sSrc, float4(p * 0.001, 0, 0)).rgb; }
void CasInput(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "cas/ffx_cas.h"
float4 main(float2 uv : TEXCOORD0) : COLOR { AF3 c; AU4 c0 = 0, c1 = 0; CasFilter(c.r, c.g, c.b, AU2(0,0), c0, c1, true); return float4(c, 1); }
