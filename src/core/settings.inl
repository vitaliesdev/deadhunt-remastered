// Settings: DeadhuntFX.ini parsing (struct Settings S). (part of the unity build, see src/main.cpp)
// ---------------------------------------------------------------- settings

struct Settings {
    bool  enabled;
    int   toggleKey, reloadKey;
    bool  smaa;
    bool  fxaa;
    float fxaaThreshold;
    bool  sharpen;
    float sharpenStrength;
    bool  bloom;
    float bloomThreshold, bloomIntensity, bloomRadius;
    bool  color;
    float exposure, gamma, contrast, saturation, vibrance, temperature;
    bool  vignette;
    float vignetteStrength, vignetteRadius;
    bool  grain;
    float grainStrength;
    bool  ao, aoDebug;
    int   debugView;
    float aoRadius, aoIntensity, aoBias, aoFadeStart, aoFadeEnd;
    bool  fog;
    float fogDensity, fogStart, fogMax, fogHeightFalloff, fogBaseHeight;
    bool  rays;
    float raysIntensity, raysDecay, raysLength, raysThreshold;
    bool  dof;
    float dofStart, dofEnd, dofStrength;
    bool  wpn;
    float wpnSpec, wpnShine, wpnRim, wpnAO;
    float handsSpec, handsShine, handsRim;
    UINT  handsW, handsH;   // size of the hands texture (0 = unknown: everything is "weapon")
    bool  hudFix, hudCover, hudMenus, hudTextFix;
    float hudScale, hudArea, hudTextScale;
    bool  shadows;
    float shLength, shThickness, shStrength, shFadeStart, shFadeEnd;
    float smRange, smSize, smSoftness;
    bool  flash;
    float flashStrength, flashRadius, flashWeapon, flashGain;
    float flashColor[3];
    bool  advEnabled;
    bool  hideHud;
    bool  newHud;
    bool  menuSkin;
};
static Settings S;

static float IniF(const wchar_t* sec, const wchar_t* key, float def)
{
    wchar_t path[MAX_PATH], buf[64], d[64];
    swprintf_s(path, L"%sDeadhuntFX.ini", g_dir);
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, path);
    return (float)_wtof(buf);
}

static void LoadSettings()
{
    S.enabled          = IniF(L"General", L"Enabled", 1) != 0;
    S.toggleKey        = (int)IniF(L"General", L"ToggleKey", VK_F9);
    S.reloadKey        = (int)IniF(L"General", L"ReloadKey", VK_F10);
    S.smaa             = IniF(L"SMAA", L"Enabled", 1) != 0;
    S.fxaa             = IniF(L"FXAA", L"Enabled", 0) != 0;
    S.fxaaThreshold    = IniF(L"FXAA", L"Threshold", 0.08f);
    S.sharpen          = IniF(L"Sharpen", L"Enabled", 1) != 0;
    S.sharpenStrength  = IniF(L"Sharpen", L"Strength", 0.6f);
    S.bloom            = IniF(L"Bloom", L"Enabled", 1) != 0;
    S.bloomThreshold   = IniF(L"Bloom", L"Threshold", 0.65f);
    S.bloomIntensity   = IniF(L"Bloom", L"Intensity", 0.35f);
    S.bloomRadius      = IniF(L"Bloom", L"Radius", 1.0f);
    S.color            = IniF(L"Color", L"Enabled", 1) != 0;
    S.exposure         = IniF(L"Color", L"Exposure", 1.0f);
    S.gamma            = IniF(L"Color", L"Gamma", 1.0f);
    S.contrast         = IniF(L"Color", L"Contrast", 1.08f);
    S.saturation       = IniF(L"Color", L"Saturation", 1.10f);
    S.vibrance         = IniF(L"Color", L"Vibrance", 0.20f);
    S.temperature      = IniF(L"Color", L"Temperature", 0.0f);
    S.vignette         = IniF(L"Vignette", L"Enabled", 1) != 0;
    S.vignetteStrength = IniF(L"Vignette", L"Strength", 0.35f);
    S.vignetteRadius   = IniF(L"Vignette", L"Radius", 0.55f);
    S.grain            = IniF(L"Grain", L"Enabled", 0) != 0;
    S.grainStrength    = IniF(L"Grain", L"Strength", 0.03f);
    S.ao               = IniF(L"AO", L"Enabled", 1) != 0;
    S.aoDebug          = IniF(L"Debug", L"ShowAO", 0) != 0;
    S.debugView        = (int)IniF(L"Debug", L"View", 0);
    S.aoRadius         = IniF(L"AO", L"Radius", 70.0f);
    S.aoIntensity      = IniF(L"AO", L"Intensity", 1.0f);
    S.aoBias           = IniF(L"AO", L"Bias", 0.15f);
    S.aoFadeStart      = IniF(L"AO", L"FadeStart", 2000.0f);
    S.aoFadeEnd        = IniF(L"AO", L"FadeEnd", 6000.0f);
    S.fog              = IniF(L"Fog", L"Enabled", 1) != 0;
    S.fogDensity       = IniF(L"Fog", L"Density", 0.00004f);
    S.fogStart         = IniF(L"Fog", L"Start", 2500.0f);
    S.fogMax           = IniF(L"Fog", L"MaxOpacity", 0.55f);
    S.fogHeightFalloff = IniF(L"Fog", L"HeightFalloff", 0.0004f);
    S.fogBaseHeight    = IniF(L"Fog", L"BaseHeight", 0.0f);
    S.rays             = IniF(L"GodRays", L"Enabled", 1) != 0;
    S.raysIntensity    = IniF(L"GodRays", L"Intensity", 0.6f);
    S.raysDecay        = IniF(L"GodRays", L"Decay", 0.97f);
    S.raysLength       = IniF(L"GodRays", L"Length", 0.85f);
    S.raysThreshold    = IniF(L"GodRays", L"Threshold", 0.45f);
    S.dof              = IniF(L"DOF", L"Enabled", 0) != 0;
    S.dofStart         = IniF(L"DOF", L"Start", 8000.0f);
    S.dofEnd           = IniF(L"DOF", L"End", 40000.0f);
    S.dofStrength      = IniF(L"DOF", L"Strength", 0.7f);
    S.wpn              = IniF(L"WeaponLight", L"Enabled", 1) != 0;
    S.wpnSpec          = IniF(L"WeaponLight", L"Specular", 0.6f);
    S.wpnShine         = IniF(L"WeaponLight", L"Shininess", 24.0f);
    S.wpnRim           = IniF(L"WeaponLight", L"Rim", 0.25f);
    S.wpnAO            = IniF(L"WeaponLight", L"SelfShadow", 0.6f);
    S.handsSpec        = IniF(L"WeaponLight", L"HandsSpecular", 0.08f);
    S.handsShine       = IniF(L"WeaponLight", L"HandsShininess", 6.0f);
    S.handsRim         = IniF(L"WeaponLight", L"HandsRim", 0.12f);
    S.handsW           = (UINT)IniF(L"WeaponLight", L"HandsTextureWidth", 0);
    S.handsH           = (UINT)IniF(L"WeaponLight", L"HandsTextureHeight", 0);
    S.hudFix           = IniF(L"HUD", L"Enabled", 1) != 0;
    S.hudScale         = IniF(L"HUD", L"Scale", 1.0f);
    S.hudArea          = IniF(L"HUD", L"Area", 0.0f);
    S.hudCover         = IniF(L"HUD", L"CoverBackgrounds", 1) != 0;
    S.hudMenus         = IniF(L"HUD", L"Menus", 0) != 0;
    S.hudTextScale     = IniF(L"HUD", L"TextScale", 1.0f);
    S.hudTextFix       = IniF(L"HUD", L"TextFix", 1) != 0;
    // patch_textsize.py makes the engine size its fonts by the screen height itself
    if (!IsBadReadPtr((void*)0x5432c4, 6) && *(unsigned char*)0x5432c6 == 0xd4) S.hudTextFix = false;
    S.shadows          = IniF(L"Shadows", L"Enabled", 1) != 0;
    S.shLength         = IniF(L"Shadows", L"Length", 1200.0f);
    S.shThickness      = IniF(L"Shadows", L"Thickness", 120.0f);
    S.shStrength       = IniF(L"Shadows", L"Strength", 0.5f);
    S.shFadeStart      = IniF(L"Shadows", L"FadeStart", 3000.0f);
    S.shFadeEnd        = IniF(L"Shadows", L"FadeEnd", 9000.0f);
    S.smRange          = IniF(L"Shadows", L"Range", 3000.0f);
    S.smSize           = IniF(L"Shadows", L"MapSize", 2048.0f);
    S.smSoftness       = IniF(L"Shadows", L"Softness", 1.5f);
    S.flash            = IniF(L"MuzzleFlash", L"Enabled", 1) != 0;
    S.flashStrength    = IniF(L"MuzzleFlash", L"Strength", 1.5f);
    S.flashRadius      = IniF(L"MuzzleFlash", L"Radius", 700.0f);
    S.flashWeapon      = IniF(L"MuzzleFlash", L"WeaponStrength", 0.8f);
    S.flashGain        = IniF(L"MuzzleFlash", L"Sensitivity", 40.0f);
    S.flashColor[0]    = IniF(L"MuzzleFlash", L"ColorR", 1.0f);
    S.flashColor[1]    = IniF(L"MuzzleFlash", L"ColorG", 0.72f);
    S.flashColor[2]    = IniF(L"MuzzleFlash", L"ColorB", 0.42f);
    S.advEnabled       = IniF(L"Advanced", L"Enabled", 1) != 0;
    S.hideHud          = IniF(L"Debug", L"HideHUD", 0) != 0;
    S.newHud           = IniF(L"HUD", L"New", 1) != 0;
    S.menuSkin         = IniF(L"HUD", L"MenuSkin", 1) != 0;
}

