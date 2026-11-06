/*
	D2GL: Diablo 2 LoD Glide/DDraw to OpenGL Wrapper.
	Copyright (C) 2023  Bayaraa

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifdef VERTEX

layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 2) in vec4 Color1;
layout(location = 3) in vec4 Color2;
layout(location = 4) in ivec2 TexIds;
layout(location = 5) in uvec4 Flags;
layout(location = 6) in vec2 Extra;

uniform mat4 u_MVP;

out vec4 v_Position;
out vec2 v_TexCoord;
out vec4 v_Color1;
out vec4 v_Color2;
flat out ivec2 v_TexIds;
flat out uvec4 v_Flags;
out vec2 v_Extra;

void main()
{
	v_Position = u_MVP * vec4(Position, 0.0, 1.0);
	gl_Position = v_Position;
	v_TexCoord = TexCoord;
	v_Color1 = Color1.abgr;
	v_Color2 = Color2.abgr;
	v_TexIds = TexIds;
	v_Flags = Flags;
	v_Extra = Extra;
}

// =============================================================
#elif FRAGMENT

layout(location = 0) out vec4 FragColor;

uniform sampler2D u_MapTexture;
uniform sampler2DArray u_CursorTexture;
uniform sampler2DArray u_FontTexture;
uniform sampler2D u_MaskTexture;
uniform sampler2DArray u_ExternalTexture;
uniform sampler2DArray u_ItemColorLUT;

in vec4 v_Position;
in vec2 v_TexCoord;
in vec4 v_Color1;
in vec4 v_Color2;
flat in ivec2 v_TexIds;
flat in uvec4 v_Flags;
in vec2 v_Extra;

uniform vec2 u_Scale;
uniform vec4 u_TextMask;
uniform vec4 u_Viewport;
uniform bool u_IsMasking = false;
uniform bool u_IsGlide = true;
uniform float u_FontSolid = 1.0;
uniform float u_FontSharp = 1.0;
uniform float u_FontBorder = 1.0;
uniform float u_FontShadowOffset = 1.5;
uniform float u_FontCenter = 0.25;

float msdf(vec3 rgb, float smoothess, float weight)
{
	float msd = max(min(rgb.r, rgb.g), min(max(rgb.r, rgb.g), rgb.b));
	float screen_px_dist = max(smoothess, 1.0) * (msd - (1.0 - 0.5 * weight));
	return clamp(screen_px_dist + 0.5, 0.0, 1.0);
}

vec3 greyscale(vec3 color, float str)
{
    float g = dot(color, vec3(0.299, 0.587, 0.114));
    return mix(color, vec3(g), str);
}

void main()
{
	switch(v_Flags.x) {
		case 1u: FragColor = texture(u_CursorTexture, vec3(v_TexCoord, v_TexIds.x)) * v_Color1; break;
		case 2u: FragColor = v_Color1; break;
		case 3u: {
			if (v_TexIds.y == 1) {
				// Runtime SDF glyph (DirectWrite): 0.5 == edge, +-6px range at a
				// 32px em. extra.x is the font pixel size, extra.y the weight.
				float sdf = texture(u_FontTexture, vec3(v_TexCoord, v_TexIds.x)).r;
				if (v_Flags.y > 0u) {
					float mlt = v_Flags.y == 1u ? 1.0 : 0.5;
					// Sample down-right for the drop shadow; 4 texels == one
					// 32px-em unit at the 128px SDF resolution. The cell padding
					// keeps this inside the glyph's own atlas slot.
					vec2 shadow_offset = vec2(4.0 / 1024.0) * u_FontShadowOffset;
					float shadow = texture(u_FontTexture, vec3(v_TexCoord - shadow_offset, v_TexIds.x)).r;
					FragColor = vec4((v_Color1.r + v_Color1.g + v_Color1.b) < 0.1 ? 0.8 : 0.0);
					FragColor.a = smoothstep(0.42, 0.58, shadow) * mlt * v_Extra.x;
				} else {
					// extra.y is an absolute pixel dilation (stroke + bold).
					float dist = (sdf * 2.0 - 1.0) * 6.0 * (v_Extra.x / 32.0) * max(u_FontSharp, 0.1) + v_Extra.y;
					float solid = max(u_FontSolid, 0.01);
					// Center brightening: deep-inside pixels mix slightly toward white
					// so thin/small SDF text reads as bright as the MSDF path.
					float center = clamp(dist * 0.75, 0.0, 1.0) * u_FontCenter;
					vec3 fill = mix(v_Color1.rgb, vec3(1.0), center);
					if (v_Flags.w == 1u) {
						float border = max(0.75, v_Extra.x * 0.04) * u_FontBorder;
						float inner = pow(clamp(dist + 0.5, 0.0, 1.0), 1.0 / solid);
						float outer = pow(clamp(dist + border + 0.5, 0.0, 1.0), 1.0 / solid);
						FragColor = vec4(mix(v_Color2.rgb, fill, inner), v_Color1.a * outer);
					} else {
						FragColor = vec4(fill, v_Color1.a * pow(clamp(dist + 0.5, 0.0, 1.0), 1.0 / solid));
					}
				}
			} else {
				vec4 color = texture(u_FontTexture, vec3(v_TexCoord, v_TexIds.x));
				if(v_Flags.y > 0u) {
					float mlt = v_Flags.y == 1u ? 1.0 : 0.5;
					FragColor = vec4((v_Color1.r + v_Color1.g + v_Color1.b) < 0.1 ? 0.8 : 0.0);
					FragColor.a = (color.a < 0.26 ? 0.0 : smoothstep(0.0, 1.0, color.a)) * mlt * v_Extra.x;
				} else {
					if (v_Flags.w == 1u) {
						float opacity1 = msdf(color.rgb, v_Extra.x, v_Extra.y + 0.02 * u_FontBorder);
						float opacity2 = msdf(color.rgb, v_Extra.x, 1.01);
						FragColor = vec4(mix(v_Color2.rgb, v_Color1.rgb, opacity2), v_Color1.a * opacity1);
					} else {
						float scale = smoothstep(1.5, 1.0, u_Scale.x);
						float opacity = msdf(color.rgb, v_Extra.x / (1.0 + scale), v_Extra.y);
						FragColor = vec4(v_Color1.rgb, v_Color1.a * opacity * (1.0 + 0.20 * scale));
					}
				}
			}
			if (u_IsMasking && v_Flags.z == 0u) {
				if (u_TextMask.x < v_Position.x && u_TextMask.z > v_Position.x && u_TextMask.y > v_Position.y && u_TextMask.w < v_Position.y)
					FragColor.a = 0.0;
				else
					FragColor.a *= 0.7;
			} else {
				FragColor.a *= v_Color2.a;
			}
		}
		break;
		case 4u: FragColor = vec4(0.0); break;
		case 5u:
			FragColor = texture(u_MapTexture, v_TexCoord);
			FragColor.rgb = greyscale(FragColor.rgb, 0.2);
			if (v_Flags.z > 0u)
				FragColor.a *= (float(v_Flags.z) * 0.01);
		break;
		case 6u:
			FragColor = v_Color1;
			float alpha = (v_TexCoord.x < 0.5) ? smoothstep(0.0, v_Extra.x, v_TexCoord.x) : smoothstep(1.0, v_Extra.y, v_TexCoord.x);
			if (v_Flags.w == 1u)
				alpha *= (v_TexCoord.y < 0.5) ? smoothstep(0.0, 0.3, v_TexCoord.y) : smoothstep(1.0, 0.7, v_TexCoord.y);
			FragColor.a *= alpha;
		break;
		case 7u:
			FragColor.rgb = v_Color2.rgb * (smoothstep(0.0, 2.0, v_TexCoord.y) / 1.4);
			FragColor.a = v_Color1.a;
		break;
		case 8u:
		{
			vec4 tc = texture(u_ExternalTexture, vec3(v_TexCoord, v_TexIds.x));
			if (tc.a < 0.01) { FragColor = tc * v_Color1; break; }
			vec3 src = tc.rgb;
			if (v_Extra.y > 0.5 && int(v_TexIds.y) > 0) {
				float ri = src.r * 31., gi = src.g * 31., bi = src.b * 31.;
				float g = round(gi);
				vec2 uv = vec2((ri + 0.5 + g * 32.) / 1024., (bi + 0.5) / 32.);
				FragColor.xyz = texture(u_ItemColorLUT, vec3(uv, float(v_TexIds.y))).rgb;
				FragColor.w = tc.w * v_Color1.w;
			} else {
				FragColor = tc * v_Color1;
			}
		}
		break;
	}

	if (v_Flags.x != 3u && v_Flags.x != 8u) {
		float border = 1.00001;
		vec2 size = vec2(border / u_Scale.x / v_Extra.x, border / u_Scale.y / v_Extra.y);
		vec2 size2 = size * 2.0, size3 = size * 3.0;
		switch (v_Flags.y) {
			case 1u:
				if(v_TexCoord.x < size.x || v_TexCoord.x > 1.0 - size.x || v_TexCoord.y < size.y || v_TexCoord.y > 1.0 - size.y)
					FragColor = v_Color2;
			case 2u:
				if ((v_TexCoord.x > size2.x && v_TexCoord.x < size3.x) || (v_TexCoord.x < 1.0 - size2.x && v_TexCoord.x > 1.0 - size3.x) ||
					(v_TexCoord.y > size2.y && v_TexCoord.y < size3.y) || (v_TexCoord.y < 1.0 - size2.y && v_TexCoord.y > 1.0 - size3.y))
					FragColor = v_Color2;
			break;
			case 3u:
				if(v_TexCoord.x < size3.x || v_TexCoord.x >= 1.0 - size3.x || v_TexCoord.y < size3.y || v_TexCoord.y >= 1.0 - size3.y)
					FragColor = v_Color1;
				if(v_TexCoord.x < size.x || v_TexCoord.x >= 1.0 - size.x || v_TexCoord.y < size.y || v_TexCoord.y >= 1.0 - size.y)
					FragColor = v_Color2;
				if ((v_TexCoord.x >= size2.x && v_TexCoord.x < size3.x) || (v_TexCoord.x < 1.0 - size2.x && v_TexCoord.x >= 1.0 - size3.x) ||
					(v_TexCoord.y >= size2.y && v_TexCoord.y < size3.y) || (v_TexCoord.y < 1.0 - size2.y && v_TexCoord.y >= 1.0 - size3.y))
					FragColor = v_Color2;
			break;
			case 4u:
				if(v_TexCoord.x < size.x || v_TexCoord.x > 1.0 - size.x || v_TexCoord.y < size.y || v_TexCoord.y > 1.0 - size.y)
					FragColor = v_Color2;
			break;
		}
		if (v_Flags.z > 0u)
			FragColor.a *= (float(v_Flags.z) * 0.01);
	}

	if (u_IsGlide && texture(u_MaskTexture, (gl_FragCoord.xy - u_Viewport.xy) / u_Viewport.zw).r > 0.1)
		FragColor.a = 0.0;
}

#endif
