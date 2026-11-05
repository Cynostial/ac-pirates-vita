#include <stdlib.h>
#include <string.h>

static char *replace_once(const char *source,const char *old,const char *replacement){
    const char *at=strstr(source,old);
    if(!at||strstr(at+strlen(old),old))return NULL;
    size_t prefix=(size_t)(at-source),n=strlen(source),a=strlen(old),b=strlen(replacement);
    char *out=malloc(n-a+b+1);
    if(!out)return NULL;
    memcpy(out,source,prefix);memcpy(out+prefix,replacement,b);
    strcpy(out+prefix+b,at+a);return out;
}

/* Four bilinear samples integrate a 4x4 reflection footprint. Tiny bright
 * reflected features must be filtered before the animated normal map moves
 * the lookup across them. The target is fixed at 480x272 by WaterReflection.
 * No render target, mip chain, or persistent CPU texture copy is allocated. */
char *acp_water_shader(const char *source){
    const char *sample="texture2D(u_Reflection, reflectionUV)";
    const char *declaration="uniform highp float u_ReflectionFactor;";
    const char *replacement="acpReflectionSample(reflectionUV)";
    const char *uniform="\nuniform highp float u_ACPReflectionFilter;";
    if(!strstr(source,"v_WaterColorAttenuation")||!strstr(source,"#define _EnableReflection2"))return NULL;
    const char *at=strstr(source,sample),*decl=strstr(source,declaration);
    if(!at||!decl||decl>at||strstr(at+strlen(sample),sample))return NULL;
    size_t n=strlen(source),u=strlen(uniform),r=strlen(replacement),old=strlen(sample);
    char *result=malloc(n+u+r-old+1);
    if(!result)return NULL;
    const char *insert=decl+strlen(declaration);
    size_t prefix=(size_t)(insert-source),middle=(size_t)(at-insert);
    memcpy(result,source,prefix);memcpy(result+prefix,uniform,u);
    memcpy(result+prefix+u,insert,middle);memcpy(result+prefix+u+middle,replacement,r);
    strcpy(result+prefix+u+middle+r,at+old);
    /* The engine switches 16 normal frames at 32 Hz. On the Vita's 30 Hz
     * presentation this skips frames periodically and jumps the reflection
     * lookup. Sample the adjacent existing texture and interpolate in time. */
    char *normals=replace_once(result,"uniform sampler2D u_NormalMap;",
        "uniform sampler2D u_NormalMap;\nuniform sampler2D u_ACPNormalNext;\nuniform highp float u_ACPNormalBlend;");
    free(result);if(!normals)return NULL;
    result=replace_once(normals,"expand(texture2D(u_NormalMap, v_TexCoord).rgb)",
        "expand(mix(texture2D(u_NormalMap, v_TexCoord).rgb, texture2D(u_ACPNormalNext, v_TexCoord).rgb, u_ACPNormalBlend))");
    free(normals);if(!result)return NULL;
    /* Integrate the distorted reflection footprint instead of using the same
     * four texels at every distance. Limit the footprint to eight texels so
     * coastal silhouettes remain recognizable and the work stays bounded. */
    const char *helpers=
        "highp vec4 acpReflectionSample(highp vec2 uv) {\n"
        "  highp vec2 pixel = vec2(1.0/480.0,1.0/272.0);\n"
        "  highp vec2 footprint = clamp(abs(dFdx(uv))+abs(dFdy(uv)),pixel,pixel*8.0);\n"
        "  highp vec2 radius = mix(pixel,footprint,step(1.5,u_ACPReflectionFilter))*min(u_ACPReflectionFilter,1.0);\n"
        "  return (texture2D(u_Reflection,uv+radius*vec2(-1.0,-1.0))+texture2D(u_Reflection,uv+radius*vec2(1.0,-1.0))+texture2D(u_Reflection,uv+radius*vec2(-1.0,1.0))+texture2D(u_Reflection,uv+radius))*0.25;\n"
        "}\n"
        "highp float acpSpecular(highp vec3 n,highp float nh,highp float exponent) {\n"
        "  highp vec3 dx=dFdx(n),dy=dFdy(n);\n"
        "  highp float variance=0.5*(dot(dx,dx)+dot(dy,dy))*step(1.5,u_ACPReflectionFilter);\n"
        "  highp float filtered=max(1.0,2.0/(2.0/(exponent+2.0)+variance)-2.0);\n"
        "  return pow(clamp(nh,0.0,1.0),filtered)*(filtered+2.0)/(exponent+2.0);\n"
        "}\n"
        "highp vec3 ADS(in highp vec3 waterColor, in highp float foam)";
    char *filtered=replace_once(result,"highp vec3 ADS(in highp vec3 waterColor, in highp float foam)",helpers);
    free(result);if(!filtered)return NULL;
    /* Broaden subpixel specular lobes using normal variance, conserving their
     * integrated energy rather than clipping away the bright output. */
    result=replace_once(filtered,"pow(nh, u_WaterSpecularExponent)","acpSpecular(n,nh,u_WaterSpecularExponent)");
    free(filtered);return result;
}
