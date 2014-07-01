/*
** gl_renderstate.cpp
** Render state maintenance
**
**---------------------------------------------------------------------------
** Copyright 2009 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "templates.h"
#include "gl/system/gl_system.h"
#include "gl/system/gl_interface.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/system/gl_cvars.h"
#include "gl/shaders/gl_shader.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_colormap.h"

void gl_SetTextureMode(int type);

FRenderState gl_RenderState;

CVAR(Bool, gl_direct_state_change, true, 0)


//==========================================================================
//
//
//
//==========================================================================

void FRenderState::Reset()
{
	mTextureEnabled = true;
	mBrightmapEnabled = mFogEnabled = mGlowEnabled = mLightEnabled = false;
	mFogColor.d = -1;
	mTextureMode = -1;
	mDesaturation = 0;
	mSrcBlend = GL_SRC_ALPHA;
	mDstBlend = GL_ONE_MINUS_SRC_ALPHA;
	glSrcBlend = glDstBlend = -1;
	glAlphaFunc = -1;
	mAlphaFunc = GL_GEQUAL;
	mAlphaThreshold = 0.5f;
	mBlendEquation = GL_FUNC_ADD;
	mObjectColor = 0xffffffff;
	glBlendEquation = -1;
	m2D = true;
	mVertexBuffer = mCurrentVertexBuffer = NULL;
	mColormapState = CM_DEFAULT;
	mLightParms[3] = -1.f;
	mSpecialEffect = EFF_NONE;
}


//==========================================================================
//
// Set texture shader info
//
//==========================================================================

void FRenderState::SetupShader(int &shaderindex, float warptime)
{
	mEffectState = shaderindex;
	if (shaderindex > 0) GLRenderer->mShaderManager->SetWarpSpeed(shaderindex, warptime);
}


//==========================================================================
//
// Apply shader settings
//
//==========================================================================

bool FRenderState::ApplyShader()
{
	FShader *activeShader;
	if (mSpecialEffect > EFF_NONE)
	{
		activeShader = GLRenderer->mShaderManager->BindEffect(mSpecialEffect);
	}
	else
	{
		activeShader = GLRenderer->mShaderManager->Get(mTextureEnabled ? mEffectState : 4);
		activeShader->Bind();
	}

	int fogset = 0;
	//glColor4fv(mColor.vec);
	if (mFogEnabled)
	{
		if ((mFogColor & 0xffffff) == 0)
		{
			fogset = gl_fogmode;
		}
		else
		{
			fogset = -gl_fogmode;
		}
	}

	glColor4fv(mColor.vec);

	activeShader->muDesaturation.Set(mDesaturation / 255.f);
	activeShader->muFogEnabled.Set(fogset);
	activeShader->muTextureMode.Set(mTextureMode);
	activeShader->muCameraPos.Set(mCameraPos.vec);
	activeShader->muLightParms.Set(mLightParms);
	activeShader->muFogColor.Set(mFogColor);
	activeShader->muObjectColor.Set(mObjectColor);
	activeShader->muDynLightColor.Set(mDynColor.vec);
	activeShader->muInterpolationFactor.Set(mInterpolationFactor);

	if (mGlowEnabled)
	{
		activeShader->muGlowTopColor.Set(mGlowTop.vec);
		activeShader->muGlowBottomColor.Set(mGlowBottom.vec);
		activeShader->muGlowTopPlane.Set(mGlowTopPlane.vec);
		activeShader->muGlowBottomPlane.Set(mGlowBottomPlane.vec);
		activeShader->currentglowstate = 1;
	}
	else if (activeShader->currentglowstate)
	{
		// if glowing is on, disable it.
		static const float nulvec[] = { 0.f, 0.f, 0.f, 0.f };
		activeShader->muGlowTopColor.Set(nulvec);
		activeShader->muGlowBottomColor.Set(nulvec);
		activeShader->muGlowTopPlane.Set(nulvec);
		activeShader->muGlowBottomPlane.Set(nulvec);
		activeShader->currentglowstate = 0;
	}

	if (mLightEnabled)
	{
		activeShader->muLightRange.Set(mNumLights);
		glUniform4fv(activeShader->lights_index, mNumLights[3], mLightData);
	}
	else
	{
		static const int nulint[] = { 0, 0, 0, 0 };
		activeShader->muLightRange.Set(nulint);
	}

	if (mColormapState != activeShader->currentfixedcolormap)
	{
		float r, g, b;
		activeShader->currentfixedcolormap = mColormapState;
		if (mColormapState == CM_DEFAULT)
		{
			activeShader->muFixedColormap.Set(0);
		}
		else if (mColormapState < CM_MAXCOLORMAP)
		{
			FSpecialColormap *scm = &SpecialColormaps[gl_fixedcolormap - CM_FIRSTSPECIALCOLORMAP];
			float m[] = { scm->ColorizeEnd[0] - scm->ColorizeStart[0],
				scm->ColorizeEnd[1] - scm->ColorizeStart[1], scm->ColorizeEnd[2] - scm->ColorizeStart[2], 0.f };

			activeShader->muFixedColormap.Set(1);
			activeShader->muColormapStart.Set(scm->ColorizeStart[0], scm->ColorizeStart[1], scm->ColorizeStart[2], 0.f);
			activeShader->muColormapRange.Set(m);
		}
		else if (mColormapState == CM_FOGLAYER)
		{
			activeShader->muFixedColormap.Set(3);
		}
		else if (mColormapState == CM_LITE)
		{
			if (gl_enhanced_nightvision)
			{
				r = 0.375f, g = 1.0f, b = 0.375f;
			}
			else
			{
				r = g = b = 1.f;
			}
			activeShader->muFixedColormap.Set(2);
			activeShader->muColormapStart.Set(r, g, b, 1.f);
		}
		else if (mColormapState >= CM_TORCH)
		{
			int flicker = mColormapState - CM_TORCH;
			r = (0.8f + (7 - flicker) / 70.0f);
			if (r > 1.0f) r = 1.0f;
			b = g = r;
			if (gl_enhanced_nightvision) b = g * 0.75f;
			activeShader->muFixedColormap.Set(2);
			activeShader->muColormapStart.Set(r, g, b, 1.f);
		}
	}
	return true;
}


//==========================================================================
//
// Apply State
//
//==========================================================================

void FRenderState::Apply()
{
	if (!gl_direct_state_change)
	{
		if (mSrcBlend != glSrcBlend || mDstBlend != glDstBlend)
		{
			glSrcBlend = mSrcBlend;
			glDstBlend = mDstBlend;
			glBlendFunc(mSrcBlend, mDstBlend);
		}
		if (mAlphaFunc != glAlphaFunc || mAlphaThreshold != glAlphaThreshold)
		{
			glAlphaFunc = mAlphaFunc;
			glAlphaThreshold = mAlphaThreshold;
			::glAlphaFunc(mAlphaFunc, mAlphaThreshold);
		}
		if (mAlphaTest != glAlphaTest)
		{
			glAlphaTest = mAlphaTest;
			if (mAlphaTest) glEnable(GL_ALPHA_TEST);
			else glDisable(GL_ALPHA_TEST);
		}
		if (mBlendEquation != glBlendEquation)
		{
			glBlendEquation = mBlendEquation;
			::glBlendEquation(mBlendEquation);
		}
	}

	if (mVertexBuffer != mCurrentVertexBuffer)
	{
		if (mVertexBuffer == NULL) glBindBuffer(GL_ARRAY_BUFFER, 0);
		else mVertexBuffer->BindVBO();
		mCurrentVertexBuffer = mVertexBuffer;
	}
	ApplyShader();
}
