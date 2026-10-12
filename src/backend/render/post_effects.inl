// Post-processing chain (AO, fog, SMAA, bloom, colour...) run on the back buffer. (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- drawing

struct Vtx { float x, y, z, w, u, v; };

static void Pass(IDirect3DDevice9* dev, int ps, IDirect3DSurface9* target, UINT tw, UINT th,
                 IDirect3DTexture9* t0, UINT sw, UINT sh,
                 IDirect3DTexture9* t1 = nullptr, IDirect3DTexture9* t2 = nullptr)
{
    dev->SetRenderTarget(0, target);
    float off[4] = { -1.0f / tw, 1.0f / th, 0, 0 };   // D3D9 half-pixel offset
    dev->SetVertexShaderConstantF(0, off, 1);
    float texel[4] = { 1.0f / sw, 1.0f / sh, (float)sw, (float)sh };
    dev->SetPixelShaderConstantF(0, texel, 1);
    dev->SetPixelShader(g_ps[ps]);
    dev->SetTexture(0, t0);
    dev->SetTexture(1, t1);
    dev->SetTexture(2, t2);
    static const Vtx q[4] = {
        { -1,  1, 0, 1, 0, 0 }, { 1,  1, 0, 1, 1, 0 },
        { -1, -1, 0, 1, 0, 1 }, { 1, -1, 0, 1, 1, 1 },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

static void SetupState(IDirect3DDevice9* dev)
{
    dev->SetVertexDeclaration(g_decl);
    dev->SetVertexShader(g_vs);
    dev->SetDepthStencilSurface(nullptr);
    for (int i = 1; i < 4; i++) dev->SetRenderTarget(i, nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    for (int s = 0; s < 16; s++) {
        DWORD filt = (s == 4 || s == 7 || s >= 14) ? D3DTEXF_POINT : D3DTEXF_LINEAR;   // SearchTex, depths
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, filt);
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, filt);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
        dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, 0);
    }
}

static void SetParams(IDirect3DDevice9* dev)
{
    static LARGE_INTEGER f0, freq;
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&f0); }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float t = (float)((now.QuadPart - f0.QuadPart) / (double)freq.QuadPart);
    t = fmodf(t, 1000.0f);

    // c1..c5: effect parameters (disabled effects get neutral values)
    float c[5][4] = {
        { S.sharpen ? S.sharpenStrength : 0, S.bloom ? S.bloomIntensity : 0, S.bloomThreshold, t },
        { S.color ? S.exposure : 1, S.color ? S.gamma : 1, S.color ? S.contrast : 1, S.color ? S.saturation : 1 },
        { S.color ? S.vibrance : 0, S.color ? S.temperature : 0,
          S.vignette ? S.vignetteStrength : 0, S.vignetteRadius },
        { S.grain ? S.grainStrength : 0, (float)g_w / (float)(g_h ? g_h : 1), S.bloomRadius, S.fxaaThreshold },
    };
    dev->SetPixelShaderConstantF(1, &c[0][0], 4);
}

// c5..c9: depth/camera based effects. depthOn = depth texture valid for this frame.
static void SetDepthParams(IDirect3DDevice9* dev, bool depthOn)
{
    DWORD fc = 0x808080;
    dev->GetRenderState(D3DRS_FOGCOLOR, &fc);
    float c[7][4] = {
        { g_zn, g_zf, g_invP11, g_invP22 },
        { S.aoRadius, S.aoIntensity, S.aoBias, (depthOn && S.ao) ? (S.aoDebug ? 2.0f : 1.0f) : 0.0f },
        { S.fogDensity, S.fogHeightFalloff, S.fogStart, (depthOn && S.fog) ? S.fogMax : 0.0f },
        { ((fc >> 16) & 255) / 255.0f, ((fc >> 8) & 255) / 255.0f, (fc & 255) / 255.0f, S.fogBaseHeight },
        { g_up[0], g_up[1], g_up[2], g_up[3] },
        { S.aoFadeStart, S.aoFadeEnd, (float)S.debugView, 0 },
        { 1.0f / g_w, 1.0f / g_h, (float)g_w, (float)g_h },   // depth buffer = back buffer size
    };
    dev->SetPixelShaderConstantF(5, &c[0][0], 7);

    // Sun on screen: direction towards the sun, rotated into view space and projected.
    float sunU = 0.5f, sunV = 0.5f, vis = 0;
    if (g_haveSun) {
        const D3DMATRIX& v = g_mainView;
        float d[3] = { -g_sunDir[0], -g_sunDir[1], -g_sunDir[2] };
        float vx = d[0] * v._11 + d[1] * v._21 + d[2] * v._31;
        float vy = d[0] * v._12 + d[1] * v._22 + d[2] * v._32;
        float vz = d[0] * v._13 + d[1] * v._23 + d[2] * v._33;
        g_sunView[0] = vx; g_sunView[1] = vy; g_sunView[2] = vz;
        g_sunInView = true;
        if (vz > 0.05f) {
            float nx = vx / (vz * g_invP11), ny = vy / (vz * g_invP22);
            sunU = nx * 0.5f + 0.5f;
            sunV = 0.5f - ny * 0.5f;
            float off = fmaxf(fmaxf(-sunU, sunU - 1), fmaxf(-sunV, sunV - 1));   // > 0 outside the screen
            vis = 1.0f - fminf(fmaxf(off / 0.6f, 0.0f), 1.0f);
            vis *= fminf((vz - 0.05f) / 0.25f, 1.0f);
        }
    }
    float r[3][4] = {
        { sunU, sunV, S.rays ? S.raysIntensity * vis : 0.0f, S.raysDecay },
        { g_sunCol[0], g_sunCol[1], g_sunCol[2], S.raysLength },
        { S.dofStart, S.dofEnd, S.dof ? S.dofStrength : 0.0f, S.raysThreshold },
    };
    dev->SetPixelShaderConstantF(12, &r[0][0], 3);

    // c22..c23: screen-space sun shadows (direction to the sun in view space)
    float sl = sqrtf(g_sunView[0] * g_sunView[0] + g_sunView[1] * g_sunView[1] + g_sunView[2] * g_sunView[2]);
    bool shOn = depthOn && S.shadows && g_haveSun && sl > 1e-4f;
    float sh[2][4] = {
        { shOn ? g_sunView[0] / sl : 0, shOn ? g_sunView[1] / sl : 0, shOn ? g_sunView[2] / sl : 0,
          shOn ? S.shStrength : 0.0f },
        { S.smRange, S.shThickness, S.shFadeStart, S.shFadeEnd },
    };
    dev->SetPixelShaderConstantF(22, &sh[0][0], 2);
}

// c15..c20: weapon lighting and muzzle flash (post stage).
static void SetWeaponParams(IDirect3DDevice9* dev, bool on, float grid[2])
{
    float aspect = (float)g_w / (float)(g_h ? g_h : 1);
    float wP11 = g_winvP11 > 0 ? g_winvP11 : 0.3f * aspect;   // the Hor+ patch makes width = 0.6*aspect*zn
    float c[7][4] = {
        { grid[0], grid[1], 0, 0 },
        { g_wzn, g_wzf, wP11, g_winvP22 },
        { g_sunView[0], g_sunView[1], g_sunView[2], (on && S.wpn && g_sunInView) ? 1.0f : 0.0f },
        { S.wpnSpec, S.wpnShine, S.wpnRim, S.wpnAO },
        { (on && S.flash && g_worldZValid) ? S.flashStrength : 0.0f, S.flashRadius, S.flashWeapon, S.flashGain },
        { S.flashColor[0], S.flashColor[1], S.flashColor[2], on ? 1.0f : 0.0f },
        { S.handsSpec, S.handsShine, S.handsRim, (on && S.handsW) ? 1.0f : 0.0f },
    };
    dev->SetPixelShaderConstantF(15, &c[0][0], 7);
}

// Effect stages:
//  STAGE_DEPTH: ambient occlusion + fog. Must run while the world is still in the depth
//               buffer: the game clears depth before drawing the weapon.
//  STAGE_POST:  SMAA, bloom, sharpen, color, vignette, grain (right before the HUD).
enum { STAGE_DEPTH = 1, STAGE_POST = 2, STAGE_ALL = 3 };

// Runs effects on the current render target.
static void RunFX(IDirect3DDevice9* dev, int stage)
{
    bool midScene = g_inScene;
    if (dev->TestCooperativeLevel() != D3D_OK)
        return;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetRenderTarget(0, &bb)) || !bb)
        return;
    D3DSURFACE_DESC desc;
    bb->GetDesc(&desc);
    if (!g_shadersOk && !BuildShaders(dev)) { bb->Release(); S.enabled = false; return; }
    if ((!g_resOk || desc.Width != g_w || desc.Height != g_h) && !CreateResources(dev, desc)) {
        bb->Release(); S.enabled = false; return;
    }

    IDirect3DSurface9 *oldRT = nullptr, *oldDS = nullptr;
    dev->GetRenderTarget(0, &oldRT);
    dev->GetDepthStencilSurface(&oldDS);
    g_sb->Capture();

    dev->StretchRect(bb, nullptr, g_scene.surf, nullptr, D3DTEXF_NONE);   // resolves MSAA
    if (!midScene) dev->BeginScene();
    if ((stage & STAGE_DEPTH) && S.shadows && g_haveSun && g_depthTex && g_have3D && g_drawsSince3D > 0)
        RenderShadowMap(dev);
    SetupState(dev);
    SetParams(dev);
    bool depthOn = (stage & STAGE_DEPTH) && g_depthTex && g_have3D && g_drawsSince3D > 0 &&
                   (S.ao || S.fog || S.rays || S.dof || S.shadows);
    if (depthOn) {
        SetDepthParams(dev, true);
        dev->SetTexture(7, g_depthTex);
        dev->SetTexture(6, nullptr);
        if (S.ao) {
            Pass(dev, PS_AO, g_ao1.surf, g_ao1.w, g_ao1.h, nullptr, g_w, g_h);
            Pass(dev, PS_AO_BLURH, g_ao2.surf, g_ao2.w, g_ao2.h, g_ao1.tex, g_ao1.w, g_ao1.h);
            Pass(dev, PS_AO_BLURV, g_ao1.surf, g_ao1.w, g_ao1.h, g_ao2.tex, g_ao2.w, g_ao2.h);
            dev->SetTexture(6, g_ao1.tex);
        }
        dev->SetTexture(8, nullptr);
        dev->SetTexture(9, nullptr);
        dev->SetTexture(13, nullptr);
        if (S.shadows && g_haveSun && g_lightOk) {
            SetShadowParams(dev);
            dev->SetTexture(14, g_smTex);
            dev->SetTexture(15, g_smTex2);
            Pass(dev, PS_SHADOW, g_sh1.surf, g_sh1.w, g_sh1.h, nullptr, g_w, g_h);
            dev->SetTexture(14, nullptr);
            dev->SetTexture(15, nullptr);
            Pass(dev, PS_AO_BLURH, g_sh2.surf, g_sh2.w, g_sh2.h, g_sh1.tex, g_sh1.w, g_sh1.h);
            Pass(dev, PS_AO_BLURV, g_sh1.surf, g_sh1.w, g_sh1.h, g_sh2.tex, g_sh2.w, g_sh2.h);
            dev->SetTexture(13, g_sh1.tex);
        }
        if (S.rays && g_haveSun) {
            Pass(dev, PS_RAY_MASK, g_ray1.surf, g_ray1.w, g_ray1.h, g_scene.tex, g_w, g_h);
            Pass(dev, PS_RAY_BLUR, g_ray2.surf, g_ray2.w, g_ray2.h, g_ray1.tex, g_ray1.w, g_ray1.h);
            dev->SetTexture(8, g_ray2.tex);
        }
        if (S.dof) {
            Pass(dev, PS_DOWN, g_dof1.surf, g_dof1.w, g_dof1.h, g_scene.tex, g_w, g_h);
            Pass(dev, PS_BLURH, g_dof2.surf, g_dof2.w, g_dof2.h, g_dof1.tex, g_dof1.w, g_dof1.h);
            Pass(dev, PS_BLURV, g_dof1.surf, g_dof1.w, g_dof1.h, g_dof2.tex, g_dof2.w, g_dof2.h);
            dev->SetTexture(9, g_dof1.tex);
        }
        Pass(dev, PS_DEPTH, bb, g_w, g_h, g_scene.tex, g_w, g_h);
        if (S.flash) {
            Pass(dev, PS_STORE_Z, g_worldZ.surf, g_w, g_h, nullptr, g_w, g_h);
            g_worldZValid = true;
        }
        for (int i = 6; i < 10; i++) dev->SetTexture(i, nullptr);
        dev->SetTexture(13, nullptr);
        if (stage & STAGE_POST)   // post effects need the image with AO/fog applied
            dev->StretchRect(bb, nullptr, g_scene.surf, nullptr, D3DTEXF_NONE);
    }

    RT* src = &g_scene;
    if (!(stage & STAGE_POST)) {
        // depth stage only
    } else if (S.smaa) {
        dev->SetTexture(3, g_areaTex);
        dev->SetTexture(4, g_searchTex);
        dev->SetRenderTarget(0, g_edges.surf);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        Pass(dev, PS_SMAA_EDGES, g_edges.surf, g_w, g_h, g_scene.tex, g_w, g_h);
        dev->SetRenderTarget(0, g_weights.surf);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        Pass(dev, PS_SMAA_WEIGHTS, g_weights.surf, g_w, g_h, g_edges.tex, g_w, g_h);
        dev->SetTexture(5, g_weights.tex);
        Pass(dev, PS_SMAA_BLEND, g_aa.surf, g_w, g_h, g_scene.tex, g_w, g_h);
        dev->SetTexture(5, nullptr);
        src = &g_aa;
    } else if (S.fxaa) {
        Pass(dev, PS_FXAA, g_aa.surf, g_w, g_h, g_scene.tex, g_w, g_h);
        src = &g_aa;
    }
    if ((stage & STAGE_POST) && S.bloom) {
        Pass(dev, PS_BRIGHT, g_q1.surf, g_q1.w, g_q1.h, src->tex, src->w, src->h);
        Pass(dev, PS_BLURH, g_q2.surf, g_q2.w, g_q2.h, g_q1.tex, g_q1.w, g_q1.h);
        Pass(dev, PS_BLURV, g_q1.surf, g_q1.w, g_q1.h, g_q2.tex, g_q2.w, g_q2.h);
        Pass(dev, PS_DOWN, g_e1.surf, g_e1.w, g_e1.h, g_q1.tex, g_q1.w, g_q1.h);
        Pass(dev, PS_BLURH, g_e2.surf, g_e2.w, g_e2.h, g_e1.tex, g_e1.w, g_e1.h);
        Pass(dev, PS_BLURV, g_e1.surf, g_e1.w, g_e1.h, g_e2.tex, g_e2.w, g_e2.h);
    }
    if (stage & STAGE_POST) {
        // After the depth stage the depth buffer holds only the weapon.
        bool wpnOn = g_depthDone && g_depthTex && (S.wpn || S.flash);
        float grid[2] = { 8, 8 };
        SetDepthParams(dev, false);
        SetWeaponParams(dev, wpnOn, grid);
        if (wpnOn && S.flash && g_worldZValid) {
            Pass(dev, PS_FLASH_MASK, g_fl32.surf, 32, 16, g_scene.tex, g_w, g_h);
            Pass(dev, PS_FLASH_REDUCE, g_fl4.surf, 4, 2, g_fl32.tex, 32, 16);
            grid[0] = 4; grid[1] = 2;
            SetWeaponParams(dev, wpnOn, grid);
            Pass(dev, PS_FLASH_REDUCE, g_fl1.surf, 1, 1, g_fl4.tex, 4, 2);
            dev->SetTexture(10, g_fl1.tex);
            dev->SetTexture(11, g_worldZ.tex);
        }
        if (wpnOn && S.wpn && S.handsW) {
            // Stencil == 2 marks the hands (tagged while the game drew them). The depth
            // surface is bound only for the stencil test; this pass does not sample it.
            dev->SetRenderTarget(0, g_handMask.surf);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
            dev->SetDepthStencilSurface(g_depthSurf);
            dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
            dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
            dev->SetRenderState(D3DRS_STENCILREF, 2);
            dev->SetRenderState(D3DRS_STENCILMASK, 0xFF);
            dev->SetRenderState(D3DRS_STENCILWRITEMASK, 0);
            dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
            dev->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
            dev->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
            Pass(dev, PS_ONE, g_handMask.surf, g_w, g_h, nullptr, g_w, g_h);
            dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            dev->SetDepthStencilSurface(nullptr);
            dev->SetTexture(12, g_handMask.tex);
        }
        dev->SetTexture(7, wpnOn ? g_depthTex : nullptr);
        Pass(dev, PS_FINAL, bb, g_w, g_h, src->tex, src->w, src->h, g_q1.tex, g_e1.tex);
        dev->SetTexture(10, nullptr);
        dev->SetTexture(11, nullptr);
        dev->SetTexture(12, nullptr);
    }
    for (int i = 3; i < 15; i++) dev->SetTexture(i, nullptr);
    if (!midScene) dev->EndScene();

    g_sb->Apply();
    dev->SetRenderTarget(0, oldRT);
    dev->SetDepthStencilSurface(oldDS);
    if (oldRT) oldRT->Release();
    if (oldDS) oldDS->Release();
    bb->Release();
}

