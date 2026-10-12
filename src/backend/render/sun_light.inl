// Sun direction from the game's directional light. (part of the unity build, see src/main.cpp)
typedef HRESULT(STDMETHODCALLTYPE* PFN_SetLight)(IDirect3DDevice9*, DWORD, const D3DLIGHT9*);
static PFN_SetLight oSetLight;

// The sun = the game's directional light.
static HRESULT STDMETHODCALLTYPE hkSetLight(IDirect3DDevice9* d, DWORD idx, const D3DLIGHT9* l)
{
    if (!g_inFX && l && l->Type == D3DLIGHT_DIRECTIONAL) {
        float len = sqrtf(l->Direction.x * l->Direction.x + l->Direction.y * l->Direction.y +
                          l->Direction.z * l->Direction.z);
        if (len > 1e-4f) {
            if (!g_haveSun)
                Log("Sol detectado: direccion (%.2f, %.2f, %.2f), color (%.2f, %.2f, %.2f)",
                    l->Direction.x / len, l->Direction.y / len, l->Direction.z / len,
                    l->Diffuse.r, l->Diffuse.g, l->Diffuse.b);
            g_sunDir[0] = l->Direction.x / len;
            g_sunDir[1] = l->Direction.y / len;
            g_sunDir[2] = l->Direction.z / len;
            float m = fmaxf(fmaxf(l->Diffuse.r, l->Diffuse.g), fmaxf(l->Diffuse.b, 1e-3f));
            g_sunCol[0] = l->Diffuse.r / m; g_sunCol[1] = l->Diffuse.g / m; g_sunCol[2] = l->Diffuse.b / m;
            g_haveSun = true;
        }
    }
    return oSetLight(d, idx, l);
}

