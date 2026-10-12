// Camera capture from SetTransform and HUD phase detection. (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- camera capture / HUD detection

static int g_presentFrames;

static void UpdateUp(const D3DMATRIX& v)
{
    g_mainView = v;
    // world = viewPos * inverse(V); for a rigid view matrix world.y = dot(viewPos, R[1]) + w
    const int k = 1;   // Y is up in Deadhunt (the culling wedge rotates with D3DXMatrixRotationY)
    g_up[0] = v.m[k][0];
    g_up[1] = v.m[k][1];
    g_up[2] = v.m[k][2];
    g_up[3] = -(v.m[3][0] * v.m[k][0] + v.m[3][1] * v.m[k][1] + v.m[3][2] * v.m[k][2]);
}


static HRESULT STDMETHODCALLTYPE hkSetTransform(IDirect3DDevice9* dev, D3DTRANSFORMSTATETYPE st, const D3DMATRIX* m)
{
    if (!g_inFX && m) {
        if (st == D3DTS_WORLD) {
            g_curWorld = *m;
        } else if (st == D3DTS_VIEW) {
            g_lastView = *m;
            if (g_curProjMain) UpdateUp(*m);
        } else if (st == D3DTS_PROJECTION) {
            g_curProjM = *m;
            bool persp = fabsf(m->_34) > 0.5f;
            if (persp && fabsf(m->_33) > 1e-6f && fabsf(1.0f - m->_33) > 1e-9f) {
                float zn = -m->_43 / m->_33, zf = m->_43 / (1.0f - m->_33);
                g_curProjMain = zf > 5000.0f;   // main camera (the weapon view uses zf = 500)
                if (!g_curProjMain && zf > 50.0f) {
                    g_wzn = zn; g_wzf = zf;
                    g_winvP11 = 1.0f / m->_11; g_winvP22 = 1.0f / m->_22;
                }
                if (g_curProjMain) {
                    g_mainProj = *m;
                    g_zn = zn; g_zf = zf;
                    g_invP11 = 1.0f / m->_11; g_invP22 = 1.0f / m->_22;
                    UpdateUp(g_lastView);
                    g_have3D = true;
                }
            } else if (!persp) {
                g_curProjMain = false;
                // Switching to 2D after the 3D world was drawn: the HUD is next.
                if (g_drawsSince3D > 0 && !g_fxDone && S.enabled) {
                    g_inFX = true;
                    RunFX(dev, g_depthDone ? STAGE_POST : STAGE_ALL);
                    g_inFX = false;
                    g_fxDone = true;
                    g_hudByProj++;
                }
            }
        }
    }
    return oSetTransform(dev, st, m);
}

