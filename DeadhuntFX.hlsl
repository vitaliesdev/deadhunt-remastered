// DeadhuntFX - shaders de posprocesado (Direct3D 9, vs_3_0 / ps_3_0)
// Se compilan al arrancar el juego. F10 en el juego los recompila.
// Los valores vienen de DeadhuntFX.ini.

// ---------------------------------------------------------------- constantes

float4 vOffset : register(c0);   // VS: desplazamiento de medio pixel

float4 cTexel  : register(c0);   // PS: 1/ancho, 1/alto, ancho, alto (textura de origen)
float4 cP1     : register(c1);   // nitidez, intensidad bloom, umbral bloom, tiempo
float4 cP2     : register(c2);   // exposicion, gamma, contraste, saturacion
float4 cP3     : register(c3);   // viveza, temperatura, fuerza vineta, radio vineta
float4 cP4     : register(c4);   // grano, aspecto, radio bloom, umbral FXAA
float4 cCam    : register(c5);   // zNear, zFar, 1/P11, 1/P22 (camara principal)
float4 cAO     : register(c6);   // radio, intensidad, sesgo, activo
float4 cFog    : register(c7);   // densidad, caida con altura, inicio, opacidad max (0 = off)
float4 cFogCol : register(c8);   // color de niebla del juego, altura base
float4 cUp     : register(c9);   // fila "arriba" (Y) de la vista inversa: altura de un punto
float4 cAO2    : register(c10);  // AO: distancia inicio desvanecido, fin desvanecido, vista depuracion
float4 cDepthTx: register(c11);  // 1/ancho, 1/alto, ancho, alto del buffer de profundidad
float4 cRay    : register(c12);  // sol en pantalla (u, v), intensidad (0 = off), decaimiento
float4 cRayCol : register(c13);  // color del sol, longitud de los rayos
float4 cDof    : register(c14);  // DOF inicio, fin, fuerza (0 = off), umbral de brillo de rayos
float4 cGrid   : register(c15);  // reduccion del fogonazo: muestras por pixel (x, y)
float4 cWCam   : register(c16);  // camara del arma: zNear, zFar, 1/P11, 1/P22
float4 cWSun   : register(c17);  // direccion al sol en espacio de vista, iluminacion del arma activa
float4 cWLit   : register(c18);  // especular, brillo (exponente), contorno, sombras propias
float4 cFlash  : register(c19);  // fuerza en el entorno (0 = off), radio, fuerza en el arma, sensibilidad
float4 cFlashC : register(c20);  // color del fogonazo, efectos de arma activos
float4 cHands  : register(c21);  // mano: especular, brillo, contorno, mascara disponible
float4 cShDir  : register(c22);  // sombras: direccion al sol (vista), fuerza (0 = off)
float4 cShPar  : register(c23);  // sombras: alcance del mapa, -, inicio y fin del desvanecido
float4 cIV0 : register(c24); float4 cIV1 : register(c25);    // vista -> mundo (columnas)
float4 cIV2 : register(c26); float4 cIV3 : register(c27);
float4 cLV0 : register(c28); float4 cLV1 : register(c29);    // mundo -> sol (columnas)
float4 cLV2 : register(c30); float4 cLV3 : register(c31);
float4 cSM  : register(c32);     // tamano del mapa, 1/tamano, suavidad, valido
float4 cLF0 : register(c33); float4 cLF1 : register(c34);    // mundo -> sol, cascada lejana
float4 cLF2 : register(c35); float4 cLF3 : register(c36);
float4 cCasc : register(c37);    // radio de la cascada cercana, radio de la lejana

sampler2D sSrc    : register(s0);
sampler2D sBloom  : register(s1);   // bloom 1/4
sampler2D sGlow   : register(s2);   // bloom 1/16 (halo amplio)
sampler2D sArea   : register(s3);   // SMAA AreaTex   (lineal)
sampler2D sSearch : register(s4);   // SMAA SearchTex (punto)
sampler2D sBlend  : register(s5);   // SMAA pesos de mezcla
sampler2D sAO     : register(s6);   // oclusion ambiental (media resolucion, filtrada)
sampler2D sDepth  : register(s7);   // profundidad de la escena (INTZ, punto)
sampler2D sRays   : register(s8);   // rayos de sol (1/4)
sampler2D sDofB   : register(s9);   // escena desenfocada (1/4) para profundidad de campo
sampler2D sFlash  : register(s10);  // fogonazo: (peso, peso*u, peso*v) medios en 1x1
sampler2D sWorldZ : register(s11);  // distancia de la escena guardada antes de dibujar el arma
sampler2D sHands  : register(s12);  // mascara de la mano (1 = mano)
sampler2D sShadow : register(s13);  // sombras del sol (media resolucion, filtradas)
sampler2D sShadowMap : register(s14);  // mapa de sombras cercano (INTZ)
sampler2D sShadowFar : register(s15);  // mapa de sombras lejano (INTZ)

static const float3 LUMA = float3(0.299, 0.587, 0.114);

// ---------------------------------------------------------------- SMAA (oficial)
// SMAA de Jorge Jimenez et al., https://github.com/iryoku/smaa (licencia MIT,
// ver DeadhuntFX/SMAA_LICENSE.txt). Se incluye sin modificar, en su modo D3D9.
#define SMAA_HLSL_3
#define SMAA_PRESET_HIGH
#define SMAA_RT_METRICS cTexel
#include "DeadhuntFX/SMAA.hlsl"

// ---------------------------------------------------------------- vertex

struct VSOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };

VSOut VS_Quad(float4 pos : POSITION, float2 uv : TEXCOORD0)
{
    VSOut o;
    o.pos = pos;
    o.pos.xy += vOffset.xy;
    o.uv = uv;
    return o;
}

float3 Tap(float2 uv, float2 d) { return tex2Dlod(sSrc, float4(uv + d * cTexel.xy, 0, 0)).rgb; }

// ---------------------------------------------------------------- suavizado (estilo FXAA)
// Detecta bordes por contraste de luminancia y mezcla a lo largo del borde.

float4 PS_FXAA(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Tap(uv, 0);
    float lC = dot(c, LUMA);
    float lN = dot(Tap(uv, float2(0, -1)), LUMA);
    float lS = dot(Tap(uv, float2(0,  1)), LUMA);
    float lW = dot(Tap(uv, float2(-1, 0)), LUMA);
    float lE = dot(Tap(uv, float2( 1, 0)), LUMA);
    float mn = min(lC, min(min(lN, lS), min(lW, lE)));
    float mx = max(lC, max(max(lN, lS), max(lW, lE)));
    float range = mx - mn;
    if (range < max(cP4.w, mx * 0.125))
        return float4(c, 1);

    float lNW = dot(Tap(uv, float2(-1, -1)), LUMA);
    float lNE = dot(Tap(uv, float2( 1, -1)), LUMA);
    float lSW = dot(Tap(uv, float2(-1,  1)), LUMA);
    float lSE = dot(Tap(uv, float2( 1,  1)), LUMA);

    // direccion perpendicular al gradiente = direccion del borde
    float2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));
    float reduce = max((lN + lS + lW + lE) * 0.03125, 1.0 / 128.0);
    float scale = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * scale, -6.0, 6.0);

    float3 a = 0.5 * (Tap(uv, dir * (1.0 / 3.0 - 0.5)) + Tap(uv, dir * (2.0 / 3.0 - 0.5)));
    float3 b = a * 0.5 + 0.25 * (Tap(uv, dir * -0.5) + Tap(uv, dir * 0.5));
    float lB = dot(b, LUMA);
    return float4((lB < mn || lB > mx) ? a : b, 1);
}

// ---------------------------------------------------------------- SMAA: 3 pasadas

float4 PS_SMAAEdges(float2 uv : TEXCOORD0) : COLOR
{
    float4 off[3];
    SMAAEdgeDetectionVS(uv, off);
    return float4(SMAALumaEdgeDetectionPS(uv, off, sSrc), 0, 0);
}

float4 PS_SMAAWeights(float2 uv : TEXCOORD0) : COLOR
{
    float2 pix;
    float4 off[3];
    SMAABlendingWeightCalculationVS(uv, pix, off);
    return SMAABlendingWeightCalculationPS(uv, pix, off, sSrc, sArea, sSearch, 0);
}

float4 PS_SMAABlend(float2 uv : TEXCOORD0) : COLOR
{
    float4 off;
    SMAANeighborhoodBlendingVS(uv, off);
    return SMAANeighborhoodBlendingPS(uv, off, sSrc, sBlend);
}

// ---------------------------------------------------------------- CAS
// Nitidez adaptativa por contraste: port a ps_3_0 de AMD FidelityFX CAS
// (https://github.com/GPUOpen-Effects/FidelityFX-CAS, licencia MIT, ver
// DeadhuntFX/CAS_LICENSE.txt). Variante sin reescalado y con "mejores
// diagonales". El original usa trucos de bits que D3D9 no admite; aqui se
// usan las operaciones exactas equivalentes.

float3 CAS(float2 uv, float sharpness)
{
    //  a b c
    //  d e f
    //  g h i
    float3 a = Tap(uv, float2(-1, -1)), b = Tap(uv, float2(0, -1)), c = Tap(uv, float2(1, -1));
    float3 d = Tap(uv, float2(-1,  0)), e = Tap(uv, 0),             f = Tap(uv, float2(1,  0));
    float3 g = Tap(uv, float2(-1,  1)), h = Tap(uv, float2(0,  1)), i = Tap(uv, float2(1,  1));

    // Minimo/maximo suaves: cruz + anillo completo.
    float3 mnCross = min(min(min(d, e), min(f, b)), h);
    float3 mxCross = max(max(max(d, e), max(f, b)), h);
    float3 mn = mnCross + min(mnCross, min(min(a, c), min(g, i)));
    float3 mx = mxCross + max(mxCross, max(max(a, c), max(g, i)));

    // Cuanto se puede reforzar sin salirse del rango de la senal.
    float3 amp = sqrt(saturate(min(mn, 2.0 - mx) / max(mx, 1e-5)));
    float peak = -1.0 / lerp(8.0, 5.0, saturate(sharpness));
    float3 w = amp * peak;
    return saturate(((b + d + f + h) * w + e) / (1.0 + 4.0 * w));
}

// ---------------------------------------------------------------- profundidad

// Centro exacto del pixel de profundidad que contiene uv. Leer la profundidad y calcular
// la posicion con el MISMO punto evita desajustes de un pixel (lineas fijas en pantalla).
// El +0.25 hace el redondeo determinista: en las pasadas a resolucion completa uv cae en
// el centro del pixel (k+0.5) y en las de media resolucion justo en la frontera (2k+1).
float2 SnapDepth(float2 uv) { return (floor(uv * cDepthTx.zw + 0.25) + 0.5) * cDepthTx.xy; }

float RawDepth(float2 uv) { return tex2Dlod(sDepth, float4(SnapDepth(uv), 0, 0)).r; }

// Profundidad del buffer (0..1) -> distancia lineal a la camara.
float LinearZ(float d) { return cCam.x * cCam.y / (cCam.y - d * (cCam.y - cCam.x)); }

// Posicion en espacio de vista a partir de la coordenada de pantalla.
float3 ViewPos(float2 uv)
{
    uv = SnapDepth(uv);
    float z = LinearZ(tex2Dlod(sDepth, float4(uv, 0, 0)).r);
    return float3((uv.x * 2 - 1) * cCam.z * z, (1 - uv.y * 2) * cCam.w * z, z);
}

bool IsSky(float d) { return d > 0.99999; }

// ---------------------------------------------------------------- oclusion ambiental
// Muestreo en espiral dentro de un radio fijo en unidades del mundo (cm),
// proyectado a pantalla. La normal se reconstruye de los vecinos mas cercanos
// en profundidad (evita halos en bordes). Ruido por pixel para romper bandas;
// el desenfoque posterior lo suaviza.

float3 ViewNormal(float2 uv, float3 P)
{
    // Vecinos a partir del centro ya ajustado: nunca coinciden con el propio pixel.
    float2 c = SnapDepth(uv);
    float2 t = cDepthTx.xy;
    float3 r = ViewPos(c + float2(t.x, 0)) - P, l = P - ViewPos(c - float2(t.x, 0));
    float3 d = ViewPos(c + float2(0, t.y)) - P, u = P - ViewPos(c - float2(0, t.y));
    float3 dx = abs(r.z) < abs(l.z) ? r : l;
    float3 dy = abs(d.z) < abs(u.z) ? d : u;
    float3 n = cross(dy, dx);
    float len2 = dot(n, n);
    if (len2 < 1e-12) return float3(0, 0, -1);
    n *= rsqrt(len2);
    return dot(n, P) > 0 ? -n : n;   // hacia la camara
}

float4 PS_AO(float2 uv : TEXCOORD0) : COLOR
{
    float d = RawDepth(uv);
    if (IsSky(d)) return 1;
    float3 P = ViewPos(uv);
    float3 N = ViewNormal(uv, P);

    // Se desvanece suavemente con la distancia (evita parpadeo y lineas de corte).
    float fade = saturate((cAO2.y - P.z) / max(cAO2.y - cAO2.x, 1));
    if (fade <= 0) return 1;

    float R = cAO.x;
    float2 rUV = R * 0.5 / P.z / float2(cCam.z, cCam.w);   // radio en coordenadas de textura

    // Patron 4x4 estable (el desenfoque posterior de 7 muestras lo elimina por completo).
    float2 cell = fmod(floor(uv * cTexel.zw * 0.5), 4);   // en pixeles de la textura de AO
    float noise = frac((cell.x * 4 + cell.y) * 0.618034 + cell.x * 0.25);
    float rot = noise * 6.2831853;

    const int N_SAMPLES = 12;
    float occ = 0;
    [unroll] for (int i = 0; i < N_SAMPLES; i++)
    {
        float a = i * 2.3999632 + rot;                 // angulo aureo
        float r = sqrt((i + 0.5) / N_SAMPLES);
        float2 o = float2(cos(a), sin(a)) * r * rUV;
        float3 v = ViewPos(uv + o) - P;
        float dist2 = dot(v, v);
        float h = dot(N, v) * rsqrt(dist2 + 1e-4);
        float nearCut = saturate(dist2 / (R * R * 0.01) - 1);   // ignora pliegues diminutos (< 10% radio)
        occ += saturate(h - cAO.z) * saturate(1 - dist2 / (R * R)) * nearCut;
    }
    float ao = saturate(1 - cAO.y * 10.0 * occ / N_SAMPLES);
    return float4(lerp(1, ao, fade).xxx, 1);
}

// Desenfoque separable que respeta bordes (no mezcla primer plano con fondo).
float BlurAO(float2 uv, float2 axis)
{
    float zc = LinearZ(RawDepth(uv));
    float sum = 0, wsum = 0;
    [unroll] for (int i = -3; i <= 3; i++)
    {
        float2 suv = uv + axis * i * cTexel.xy;
        float z = LinearZ(RawDepth(suv));
        float w = exp(-i * i * 0.18) * saturate(1 - abs(z - zc) / (zc * 0.05 + 1));
        sum += tex2Dlod(sSrc, float4(suv, 0, 0)).r * w;
        wsum += w;
    }
    return sum / max(wsum, 1e-4);
}
float4 PS_AOBlurH(float2 uv : TEXCOORD0) : COLOR { return float4(BlurAO(uv, float2(1, 0)).xxx, 1); }
float4 PS_AOBlurV(float2 uv : TEXCOORD0) : COLOR { return float4(BlurAO(uv, float2(0, 1)).xxx, 1); }

// ---------------------------------------------------------------- bloom

// Reduce a 1/4 quedandose solo con lo brillante (umbral suave).
float4 PS_Bright(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = 0.25 * (Tap(uv, float2(-1, -1)) + Tap(uv, float2(1, -1)) +
                       Tap(uv, float2(-1,  1)) + Tap(uv, float2(1,  1)));
    float l = dot(c, LUMA);
    float knee = 0.15;
    float soft = clamp(l - cP1.z + knee, 0, 2 * knee);
    soft = soft * soft / (4 * knee + 1e-4);
    float w = max(soft, l - cP1.z) / max(l, 1e-4);
    return float4(c * w, 1);
}

float4 PS_Down(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = 0.25 * (Tap(uv, float2(-1, -1)) + Tap(uv, float2(1, -1)) +
                       Tap(uv, float2(-1,  1)) + Tap(uv, float2(1,  1)));
    return float4(c, 1);
}

// Gaussiana de 9 muestras usando filtrado bilineal (5 lecturas).
float3 Blur(float2 uv, float2 axis)
{
    float r = cP4.z;
    float3 c = Tap(uv, 0) * 0.2270270270;
    c += (Tap(uv,  axis * 1.3846153846 * r) + Tap(uv, -axis * 1.3846153846 * r)) * 0.3162162162;
    c += (Tap(uv,  axis * 3.2307692308 * r) + Tap(uv, -axis * 3.2307692308 * r)) * 0.0702702703;
    return c;
}
float4 PS_BlurH(float2 uv : TEXCOORD0) : COLOR { return float4(Blur(uv, float2(1, 0)), 1); }
float4 PS_BlurV(float2 uv : TEXCOORD0) : COLOR { return float4(Blur(uv, float2(0, 1)), 1); }

// ---------------------------------------------------------------- sombras del sol
// Mapa de sombras: la escena se dibuja tambien desde el sol (en el modulo). Aqui cada
// pixel se lleva a coordenadas del sol y se compara con lo que el sol "ve" en ese punto.

static const float2 POISSON[16] = {
    float2(-0.942, -0.399), float2( 0.946, -0.769), float2(-0.094, -0.929), float2( 0.345,  0.294),
    float2(-0.916,  0.458), float2(-0.815, -0.879), float2(-0.383,  0.277), float2( 0.975,  0.756),
    float2( 0.443, -0.975), float2( 0.537, -0.474), float2(-0.265, -0.419), float2( 0.792,  0.191),
    float2(-0.242,  0.997), float2(-0.814,  0.914), float2( 0.200,  0.786), float2( 0.144, -0.141) };

// Fraccion iluminada de un punto del mundo en una cascada (-1 = fuera de la cascada).
float CascadeLit(sampler2D sm, float4 c0, float4 c1, float4 c2, float4 W, float2 rot, out float edge)
{
    float3 L = float3(dot(W, c0), dot(W, c1), dot(W, c2));
    float2 suv = float2(L.x * 0.5 + 0.5, 0.5 - L.y * 0.5);
    float2 e = min(suv, 1 - suv);
    edge = saturate(min(e.x, e.y) * 20);
    if (any(suv < 0.002) || any(suv > 0.998) || L.z > 1) return -1;
    float zc = L.z - 0.00005;
    // rotacion del patron fijada al mapa de sombras (al mundo), no a la pantalla: asi el ruido
    // de los bordes no se arrastra al girar o andar
    float2 cell = floor(suv * cSM.x * 0.5);
    float ang = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    rot = float2(cos(ang), sin(ang));
    float lit = 0;
    [unroll] for (int i = 0; i < 16; i++)
    {
        float2 p = POISSON[i];
        float2 o = float2(p.x * rot.x - p.y * rot.y, p.x * rot.y + p.y * rot.x) * cSM.z * cSM.y;
        lit += tex2Dlod(sm, float4(suv + o, 0, 0)).r >= zc ? 1 : 0;
    }
    return lit / 16;
}

float4 PS_Shadow(float2 uv : TEXCOORD0) : COLOR
{
    float d = RawDepth(uv);
    if (IsSky(d) || cSM.w <= 0) return 1;
    float3 P = ViewPos(uv);
    float fade = saturate((cShPar.w - P.z) / max(cShPar.w - cShPar.z, 1));
    if (fade <= 0) return 1;

    float3 N = ViewNormal(uv, P);
    float facing = saturate(dot(N, cShDir.xyz) * 4);   // caras de espaldas al sol: ya oscuras
    if (facing <= 0) return 1;

    float2 rot = float2(1, 0);   // CascadeLit la calcula por texel del mapa

    // cascada cercana (mas detalle); si el punto no cae dentro, la lejana
    float edge0, edge1;
    float4 v0 = float4(P + N * (2 * cCasc.x * cSM.y) * 1.5, 1);
    float4 W0 = float4(dot(v0, cIV0), dot(v0, cIV1), dot(v0, cIV2), 1);
    float lit0 = CascadeLit(sShadowMap, cLV0, cLV1, cLV2, W0, rot, edge0);
    float4 v1 = float4(P + N * (2 * cCasc.y * cSM.y) * 1.5, 1);
    float4 W1 = float4(dot(v1, cIV0), dot(v1, cIV1), dot(v1, cIV2), 1);
    float lit1 = CascadeLit(sShadowFar, cLF0, cLF1, cLF2, W1, rot, edge1);
    if (lit1 < 0) lit1 = 1, edge1 = 0;
    float lit = lit0 < 0 ? lerp(1, lit1, edge1) : lerp(lit1, lit0, edge0);   // transicion suave
    float shadow = 1 - lit;
    return float4((1 - shadow * facing * fade).xxx, 1);
}

// ---------------------------------------------------------------- rayos de sol
// Dispersion de luz en pantalla: el cielo brillante se difumina radialmente desde la
// posicion del sol, de modo que los objetos delante del cielo "cortan" los rayos.

float4 PS_RayMask(float2 uv : TEXCOORD0) : COLOR
{
    // 4 lecturas por pixel de 1/4 de resolucion
    float3 sum = 0;
    [unroll] for (int i = 0; i < 4; i++)
    {
        float2 o = (float2(fmod(i, 2), floor(i * 0.5)) - 0.5) * 2 * cTexel.xy;
        float2 suv = uv + o;
        if (IsSky(RawDepth(suv)))
        {
            float3 c = tex2Dlod(sSrc, float4(suv, 0, 0)).rgb;
            sum += c * saturate((dot(c, LUMA) - cDof.w) / (1 - cDof.w + 1e-3));
        }
    }
    // mas fuerte cerca del sol
    float2 d = (uv - cRay.xy) * float2(cP4.y, 1);
    return float4(sum * 0.25 * saturate(1 - length(d) * 0.6), 1);
}

float4 PS_RayBlur(float2 uv : TEXCOORD0) : COLOR
{
    const int N = 64;
    float2 delta = (uv - cRay.xy) * (cRayCol.w / N);
    float2 cell = fmod(floor(uv * cTexel.zw), 2);
    float2 p = uv - delta * (cell.x * 0.5 + cell.y * 0.25);   // pequeno desfase: evita bandas
    float3 sum = 0;
    float w = 1, wsum = 0;
    [loop] for (int i = 0; i < N; i++)
    {
        sum += tex2Dlod(sSrc, float4(p, 0, 0)).rgb * w;
        wsum += w;
        w *= cRay.w;
        p -= delta;
    }
    return float4(sum / max(wsum, 1e-3) * 4, 1);
}

// ---------------------------------------------------------------- arma y fogonazo

float4 PS_One(float2 uv : TEXCOORD0) : COLOR { return 1; }

// Guarda la distancia del mundo (el juego borra la profundidad antes de dibujar el arma).
float4 PS_StoreZ(float2 uv : TEXCOORD0) : COLOR
{
    float d = RawDepth(uv);
    return float4(IsSky(d) ? 1e6 : LinearZ(d), 0, 0, 1);
}

// Pixeles muy brillantes y calidos = fogonazo. Cada pixel de 32x16 resume su zona.
float4 PS_FlashMask(float2 uv : TEXCOORD0) : COLOR
{
    float2 cellSize = float2(1.0 / 32, 1.0 / 16);
    float3 acc = 0;
    [unroll] for (int y = 0; y < 8; y++)
    [unroll] for (int x = 0; x < 8; x++)
    {
        float2 suv = uv + (float2(x, y) + 0.5 - 4) / 8 * cellSize;
        float3 c = tex2Dlod(sSrc, float4(suv, 0, 0)).rgb;
        float w = smoothstep(0.75, 0.95, dot(c, LUMA)) * saturate((c.r - c.b) * 3);
        acc += float3(w, w * suv.x, w * suv.y);
    }
    return float4(acc / 64, 1);
}

float4 PS_FlashReduce(float2 uv : TEXCOORD0) : COLOR
{
    float3 acc = 0;
    for (int y = 0; y < cGrid.y; y++)
    for (int x = 0; x < cGrid.x; x++)
    {
        float2 suv = uv + (float2(x, y) + 0.5 - cGrid.xy * 0.5) * cTexel.xy;
        acc += tex2Dlod(sSrc, float4(suv, 0, 0)).rgb;
    }
    return float4(acc / (cGrid.x * cGrid.y), 1);
}

// Posicion del arma en su propio espacio de vista (su profundidad usa otra camara).
float3 WeaponPos(float2 uv)
{
    uv = SnapDepth(uv);
    float d = tex2Dlod(sDepth, float4(uv, 0, 0)).r;
    float z = cWCam.x * cWCam.y / (cWCam.y - d * (cWCam.y - cWCam.x));
    return float3((uv.x * 2 - 1) * cWCam.z * z, (1 - uv.y * 2) * cWCam.w * z, z);
}

float3 WeaponNormal(float2 uv, float3 P)
{
    float2 c = SnapDepth(uv);
    float2 t = cDepthTx.xy;
    float3 r = WeaponPos(c + float2(t.x, 0)) - P, l = P - WeaponPos(c - float2(t.x, 0));
    float3 d = WeaponPos(c + float2(0, t.y)) - P, u = P - WeaponPos(c - float2(0, t.y));
    float3 dx = abs(r.z) < abs(l.z) ? r : l;
    float3 dy = abs(d.z) < abs(u.z) ? d : u;
    float3 n = cross(dy, dx);
    float len2 = dot(n, n);
    if (len2 < 1e-14) return float3(0, 0, -1);
    n *= rsqrt(len2);
    return dot(n, P) > 0 ? -n : n;
}

// Sombras en los huecos del propio arma (radio pequeno en su escala).
float WeaponAO(float2 uv, float3 P, float3 N)
{
    const float R = 2.0;
    float2 rUV = R * 0.5 / P.z / cWCam.zw;
    float occ = 0;
    [unroll] for (int i = 0; i < 8; i++)
    {
        float a = i * 2.3999632;
        float2 o = float2(cos(a), sin(a)) * sqrt((i + 0.5) / 8) * rUV;
        float d = RawDepth(uv + o);
        if (IsSky(d)) continue;
        float3 v = WeaponPos(uv + o) - P;
        float dist2 = dot(v, v);
        occ += saturate(dot(N, v) * rsqrt(dist2 + 1e-5) - 0.2) * saturate(1 - dist2 / (R * R));
    }
    return saturate(1 - occ / 8 * 2.5);
}

float3 WeaponAndFlash(float2 uv, float3 c)
{
    if (cFlashC.w <= 0) return c;

    // fogonazo: intensidad y posicion en pantalla
    float flashI = 0;
    float2 flashUV = float2(0.5, 0.6);
    if (cFlash.x > 0)
    {
        float3 f = tex2Dlod(sFlash, float4(0.5, 0.5, 0, 0)).rgb;
        flashI = saturate(f.r * cFlash.w);
        if (f.r > 1e-5) flashUV = f.gb / f.r;
    }

    float d = RawDepth(uv);
    if (!IsSky(d))
    {
        // --- pixel del arma
        float3 P = WeaponPos(uv);
        float3 N = WeaponNormal(uv, P);
        float3 V = -normalize(P);
        if (cWSun.w > 0)
        {
            // Material: metal del arma o guante/piel de la mano
            float hand = cHands.w > 0 ? tex2Dlod(sHands, float4(uv, 0, 0)).r : 0;
            float specI = lerp(cWLit.x, cHands.x, hand);
            float shine = lerp(cWLit.y, cHands.y, hand);
            float rimI  = lerp(cWLit.z, cHands.z, hand);

            float3 L = normalize(cWSun.xyz);
            float3 H = normalize(L + V);
            float ndl = saturate(dot(N, L));
            float spec = pow(saturate(dot(N, H)), shine) * ndl;
            float rim = pow(1 - saturate(dot(N, V)), 3);
            float ao = lerp(1, WeaponAO(uv, P, N), cWLit.w);
            c *= ao * lerp(0.85, 1.1, ndl);
            c += cRayCol.rgb * (spec * specI + rim * rimI * 0.5) * ao;
        }
        c += c * cFlashC.rgb * flashI * cFlash.z * 2;
        return c;
    }

    // --- pixel del mundo: luz del fogonazo segun distancia y orientacion
    if (flashI > 0.001)
    {
        float2 su = SnapDepth(uv);
        float z = tex2Dlod(sWorldZ, float4(su, 0, 0)).r;
        if (z < 1e5)
        {
            float3 Pw = float3((su.x * 2 - 1) * cCam.z * z, (1 - su.y * 2) * cCam.w * z, z);
            float2 t = cDepthTx.xy;
            float zr = tex2Dlod(sWorldZ, float4(su + float2(t.x, 0), 0, 0)).r;
            float zd = tex2Dlod(sWorldZ, float4(su + float2(0, t.y), 0, 0)).r;
            float3 Pr = float3(((su.x + t.x) * 2 - 1) * cCam.z * zr, (1 - su.y * 2) * cCam.w * zr, zr);
            float3 Pd = float3((su.x * 2 - 1) * cCam.z * zd, (1 - (su.y + t.y) * 2) * cCam.w * zd, zd);
            float3 Nw = normalize(cross(Pd - Pw, Pr - Pw));
            if (dot(Nw, Pw) > 0) Nw = -Nw;
            const float LZ = 60;   // el cañon, unos 60 cm delante de la camara
            float3 Lp = float3((flashUV.x * 2 - 1) * cCam.z * LZ, (1 - flashUV.y * 2) * cCam.w * LZ, LZ);
            float3 toL = Lp - Pw;
            float dist2 = dot(toL, toL);
            float atten = cFlash.y * cFlash.y / (cFlash.y * cFlash.y + dist2);
            float ndl = saturate(dot(Nw, toL * rsqrt(dist2)));
            c += c * cFlashC.rgb * flashI * cFlash.x * atten * (0.25 + 0.75 * ndl);
        }
    }
    return c;
}

// ---------------------------------------------------------------- pasada de profundidad

// Amplia una textura de media resolucion (AO, sombras) sin mezclar objetos con el fondo.
float UpsampleTex(sampler2D tex, float2 uv)
{
    float zc = LinearZ(RawDepth(uv));
    float2 ht = cTexel.xy * 2;                        // texel de la textura de AO
    float2 f = uv / ht - 0.5;
    float2 base = (floor(f) + 0.5) * ht;
    float2 fr = frac(f);
    float sum = 0, wsum = 0;
    [unroll] for (int j = 0; j < 4; j++)
    {
        float2 o = float2(fmod(j, 2), floor(j * 0.5));
        float2 suv = base + o * ht;
        float z = LinearZ(RawDepth(suv));
        float wb = (o.x ? fr.x : 1 - fr.x) * (o.y ? fr.y : 1 - fr.y);
        float w = wb / (abs(z - zc) / (zc * 0.02 + 1) + 0.001);
        sum += tex2Dlod(tex, float4(suv, 0, 0)).r * w;
        wsum += w;
    }
    return wsum > 0 ? sum / wsum : 1;
}
// Se ejecuta antes de que el juego borre la profundidad para dibujar el arma.

float4 PS_Depth(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Tap(uv, 0);

    // Profundidad de campo lejana: mezcla con la escena desenfocada segun la distancia.
    if (cDof.z > 0)
    {
        float d = RawDepth(uv);
        float z = IsSky(d) ? cDof.y : LinearZ(d);
        float a = saturate((z - cDof.x) / max(cDof.y - cDof.x, 1)) * cDof.z;
        c = lerp(c, tex2Dlod(sDofB, float4(uv, 0, 0)).rgb, a);
    }

    // Sombras del sol
    if (cShDir.w > 0)
        c *= lerp(1, 1 - cShDir.w, 1 - UpsampleTex(sShadow, uv));

    // Vistas de depuracion ([Debug] View): 1 = oclusion, 2 = orientacion, 3 = profundidad
    if (cAO2.z > 2.5)
    {
        float d = RawDepth(uv);
        if (IsSky(d)) return float4(0.2, 0.4, 1, 1);
        float z = LinearZ(d);
        return float4(frac(z / 100).xxx * 0.7 + 0.3 * saturate(1 - z / 20000), 1);   // bandas cada 100 unidades
    }
    if (cAO2.z > 1.5)
    {
        float d = RawDepth(uv);
        if (IsSky(d)) return float4(0, 0, 0, 1);
        return float4(ViewNormal(uv, ViewPos(uv)) * 0.5 + 0.5, 1);
    }
    if (cAO.w > 1.5 || cAO2.z > 0.5)
        return float4(UpsampleTex(sAO, uv).xxx, 1);
    if (cAO.w > 0)
        c *= UpsampleTex(sAO, uv);

    // Niebla atmosferica por distancia, mas densa en zonas bajas (Y = arriba).
    if (cFog.w > 0)
    {
        float d = RawDepth(uv);
        if (!IsSky(d))
        {
            float3 P = ViewPos(uv);
            float height = dot(float4(P, 1), cUp) - cFogCol.w;
            float f = 1 - exp(-cFog.x * max(length(P) - cFog.z, 0));
            f *= exp(-cFog.y * max(height, 0));
            c = lerp(c, cFogCol.rgb, saturate(f) * cFog.w);
        }
    }

    // Rayos de sol: suma con mezcla "trama" para no quemar.
    if (cRay.z > 0)
    {
        float3 r = saturate(tex2Dlod(sRays, float4(uv, 0, 0)).rgb * cRayCol.rgb * cRay.z);
        c = 1 - (1 - c) * (1 - r);
    }

    return float4(c, 1);
}

// ---------------------------------------------------------------- pasada final

float Hash(float2 p)
{
    p = frac(p * float2(443.897, 441.423));
    p += dot(p, p.yx + 19.19);
    return frac((p.x + p.y) * p.x);
}

float4 PS_Final(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Tap(uv, 0);

    // Nitidez adaptativa (AMD CAS)
    if (cP1.x > 0)
        c = CAS(uv, cP1.x);

    // Iluminacion del arma y luz del fogonazo
    c = WeaponAndFlash(uv, c);

    // Bloom: mezcla "trama" para no quemar las zonas claras.
    if (cP1.y > 0)
    {
        float3 b = tex2Dlod(sBloom, float4(uv, 0, 0)).rgb * 0.6 +
                   tex2Dlod(sGlow,  float4(uv, 0, 0)).rgb * 0.8;
        b = saturate(b * cP1.y);
        c = 1 - (1 - c) * (1 - b);
    }

    // Color
    c *= cP2.x;                                        // exposicion
    c = pow(saturate(c), 1.0 / cP2.y);                 // gamma
    c = saturate((c - 0.5) * cP2.z + 0.5);             // contraste
    float l = dot(c, LUMA);
    c = lerp(l.xxx, c, cP2.w);                         // saturacion
    float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    c = lerp(l.xxx, c, 1 + cP3.x * (1 - sat));         // viveza (afecta mas a lo apagado)
    c *= float3(1 + cP3.y * 0.08, 1, 1 - cP3.y * 0.08); // temperatura (+ calido, - frio)
    c = saturate(c);

    // Vineta con proporcion correcta (ultrawide)
    if (cP3.z > 0)
    {
        float2 d = (uv - 0.5) * float2(cP4.y, 1);
        float dist = length(d) / length(float2(cP4.y, 1) * 0.5);
        c *= 1 - cP3.z * smoothstep(cP3.w, 1.15, dist);
    }

    // Grano
    if (cP4.x > 0)
    {
        float g = Hash(uv * cTexel.zw + frac(cP1.w * 13.17) * 1000) - 0.5;
        c += g * cP4.x * (1 - l * 0.5);
    }

    return float4(c, 1);
}
