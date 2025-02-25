#include <cstring>

#include <TFE_System/profiler.h>
#include <TFE_System/math.h>
#include <TFE_Asset/modelAsset_jedi.h>
#include <TFE_Game/igame.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Level/rsector.h>
#include <TFE_Jedi/Level/robject.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>

#include <TFE_Input/input.h>

#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_RenderBackend/indexBuffer.h>
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/shaderBuffer.h>

#include "modelGPU.h"
#include "frustum.h"
#include "../rcommon.h"

#include <map>
#include <algorithm>

using namespace TFE_RenderBackend;

namespace TFE_Jedi
{
	enum ModelShader
	{
		MGPU_SHADER_SOLID = 0,
		MGPU_SHADER_HOLOGRAM,
		MGPU_SHADER_TRANS,
		MGPU_SHADER_COUNT
	};

	enum Constants
	{
		MGPU_MAX_MODELS = 1024,
		MGPU_MAX_3DO_PER_PASS = 512,
	};

	struct ModelVertex
	{
		Vec3f pos;
		Vec3f nrm;
		Vec2f uv;
		u32 color;
	};

	struct ModelGPU
	{
		ModelShader shader;
		s32 indexStart;
		s32 polyCount;
	};

	struct ModelDraw
	{
		s32 modelId;
		Vec3f posWS;
		Vec2f floorOffset;
		f32 transform[9];
		f32 ambient;
	};

	static const AttributeMapping c_modelAttrMapping[] =
	{
		{ATTR_POS,   ATYPE_FLOAT, 3, 0, false},
		{ATTR_NRM,   ATYPE_FLOAT, 3, 0, false},
		{ATTR_UV,    ATYPE_FLOAT, 2, 0, false},
		{ATTR_COLOR, ATYPE_UINT8, 4, 0, true},
	};
	static const u32 c_modelAttrCount = TFE_ARRAYSIZE(c_modelAttrMapping);

	static Shader s_modelShaders[MGPU_SHADER_COUNT];
	static VertexBuffer s_modelVertexBuffer;	// vertex buffer contains vertices for all loaded models.
	static IndexBuffer  s_modelIndexBuffer;		// index buffer for all loaded models.

	static std::vector<ModelVertex> s_vertexData;
	static std::vector<u32> s_indexData;

	static s32 s_mgpu_cameraPosId[MGPU_SHADER_COUNT];
	static s32 s_mgpu_cameraViewId[MGPU_SHADER_COUNT];
	static s32 s_mgpu_cameraProjId[MGPU_SHADER_COUNT];
	static s32 s_mgpu_cameraDirId[MGPU_SHADER_COUNT];
	static s32 s_mgpu_lightDataId[MGPU_SHADER_COUNT];
	static s32 s_mgpu_modelMtxId[MGPU_SHADER_COUNT];
	static s32 s_mgpu_modelPosId[MGPU_SHADER_COUNT];
	static s32 s_mgpu_cameraRightId[MGPU_SHADER_COUNT];

	static ModelGPU s_models[MGPU_MAX_MODELS];
	static ModelDraw s_modelDrawList[MGPU_SHADER_COUNT][MGPU_MAX_3DO_PER_PASS];
	static s32 s_modelDrawListCount[MGPU_SHADER_COUNT];
	static s32 s_modelCount;

	extern Mat3  s_cameraMtx;
	extern Mat4  s_cameraProj;
	extern Vec3f s_cameraPos;
	extern Vec3f s_cameraDir;
	extern Vec3f s_cameraRight;
	
	const char* c_vertexShaders[MGPU_SHADER_COUNT] = 
	{
		"Shaders/gpu_render_modelSolid.vert",
		"Shaders/gpu_render_modelHologram.vert",
		"Shaders/gpu_render_modelSolid.vert",
	};
	const char* c_fragmentShaders[MGPU_SHADER_COUNT] =
	{
		"Shaders/gpu_render_modelSolid.frag",
		"Shaders/gpu_render_modelHologram.frag",
		"Shaders/gpu_render_modelSolid.frag",
	};

	bool model_buildShaderVariant(ModelShader variant, s32 defineCount, ShaderDefine* defines)
	{
		Shader* shader = &s_modelShaders[variant];
		if (!shader->load(c_vertexShaders[variant], c_fragmentShaders[variant], defineCount, defines, SHADER_VER_STD))
		{
			return false;
		}

		s_mgpu_cameraPosId[variant]   = shader->getVariableId("CameraPos");
		s_mgpu_cameraViewId[variant]  = shader->getVariableId("CameraView");
		s_mgpu_cameraProjId[variant]  = shader->getVariableId("CameraProj");
		s_mgpu_cameraDirId[variant]   = shader->getVariableId("CameraDir");
		s_mgpu_cameraRightId[variant] = shader->getVariableId("CameraRight");
		s_mgpu_modelMtxId[variant]    = shader->getVariableId("ModelMtx");
		s_mgpu_modelPosId[variant]    = shader->getVariableId("ModelPos");
		s_mgpu_lightDataId[variant]   = shader->getVariableId("LightData");
		
		shader->bindTextureNameToSlot("Palette", 0);
		shader->bindTextureNameToSlot("Colormap", 1);
		shader->bindTextureNameToSlot("Textures", 2);
		shader->bindTextureNameToSlot("TextureTable", 3);
		return true;
	}

	bool model_init()
	{
		bool result = true;
		for (s32 i = 0; i < MGPU_SHADER_COUNT - 1; i++)
		{
			result = result && model_buildShaderVariant(ModelShader(i), 0, nullptr);
		}

		ShaderDefine defines[] =
		{
			{"MODEL_TRANSPARENT_PASS", "1"}
		};
		result = result && model_buildShaderVariant(MGPU_SHADER_TRANS, TFE_ARRAYSIZE(defines), defines);
		return result;
	}

	void model_destroy()
	{
		for (s32 i = 0; i < MGPU_SHADER_COUNT; i++)
		{
			s_modelShaders[i].destroy();
		}
	}

	void buildModelDrawVertices(JediModel* model, s32* indexStart, s32* vertexStart)
	{
		if (s_modelCount >= MGPU_MAX_MODELS) { return; }

		// In this version, we render one quad per vertex.
		// Store store 4 vertices per quad, but store corner in uv.
		u32 vcount = 4 * model->vertexCount;
		u32 icount = 6 * model->vertexCount;
		u32 vidx = *vertexStart;
		(*vertexStart) += vcount;
		(*indexStart) += icount;

		const size_t curVtxSize = s_vertexData.size();
		const size_t curIdxSize = s_indexData.size();
		s_vertexData.resize(curVtxSize + vcount);
		s_indexData.resize(curIdxSize + icount);

		const vec3* srcVtx = model->vertices;
		const u32 color = model->polygons[0].color;

		ModelVertex* outVtx = s_vertexData.data() + curVtxSize;
		u32* outIdx = s_indexData.data() + curIdxSize;
		for (s32 v = 0; v < model->vertexCount; v++, outVtx += 4, vidx += 4, outIdx += 6)
		{
			Vec3f pos = { fixed16ToFloat(srcVtx[v].x), fixed16ToFloat(srcVtx[v].y), fixed16ToFloat(srcVtx[v].z) };
			Vec3f nrm = { 0 };

			// Build a quad.
			outVtx[0].pos = pos;
			outVtx[0].nrm = nrm;
			outVtx[0].uv = { 0.0f, 1.0f };
			outVtx[0].color = color;

			outVtx[1].pos = pos;
			outVtx[1].nrm = nrm;
			outVtx[1].uv = { 1.0f, 1.0f };
			outVtx[1].color = color;

			outVtx[2].pos = pos;
			outVtx[2].nrm = nrm;
			outVtx[2].uv = { 1.0f, 0.0f };
			outVtx[2].color = color;

			outVtx[3].pos = pos;
			outVtx[3].nrm = nrm;
			outVtx[3].uv = { 0.0f, 0.0f };
			outVtx[3].color = color;

			// Indices.
			outIdx[0] = 0 + vidx;
			outIdx[1] = 1 + vidx;
			outIdx[2] = 2 + vidx;

			outIdx[3] = 0 + vidx;
			outIdx[4] = 2 + vidx;
			outIdx[5] = 3 + vidx;
		}

		s_models[s_modelCount].indexStart = (s32)curIdxSize;
		s_models[s_modelCount].polyCount = model->vertexCount * 2;
		s_models[s_modelCount].shader = MGPU_SHADER_HOLOGRAM;
		model->drawId = s_modelCount;
		s_modelCount++;
	}

	JediModel* s_curModel = nullptr;
	s32* s_curIndexStart;
	s32* s_curVertexStart;

	struct CompositeVertex
	{
		u32  index;
		vec3 pos;
		vec2 uv;
		vec3 nrml;
		s32 textureId;
		u8 color;
		u8 planeMode;
	};
	std::vector<CompositeVertex> s_modelVertexList;
	std::map<u32, std::vector<u32>> s_modelVertexMap;
	static s32 s_verticesMerged = 0;
	static bool s_modelTrans = false;

	bool startModel(JediModel* model, s32* indexStart, s32* vertexStart)
	{
		if (s_modelCount >= MGPU_MAX_MODELS) { return false; }

		s_curModel = model;
		s_curIndexStart = indexStart;
		s_curVertexStart = vertexStart;
		s_modelVertexMap.clear();
		s_modelVertexList.clear();
		s_modelTrans = false;
		return true;
	}

	void endModel()
	{
		// Create the entry.
		s_models[s_modelCount].indexStart = *s_curIndexStart;
		s_models[s_modelCount].polyCount = ((s32)s_indexData.size() - (*s_curIndexStart)) / 3;
		s_models[s_modelCount].shader = s_modelTrans ? MGPU_SHADER_TRANS : MGPU_SHADER_SOLID;
		s_curModel->drawId = s_modelCount;
		s_modelCount++;

		// Add vertices.
		const u32 vtxCount = (u32)s_modelVertexList.size();
		const CompositeVertex* srcVtx = s_modelVertexList.data();

		s_vertexData.resize((*s_curVertexStart) + vtxCount);
		ModelVertex* outVtx = s_vertexData.data() + (*s_curVertexStart);
		for (u32 v = 0; v < vtxCount; v++, outVtx++, srcVtx++)
		{
			outVtx->pos.x = fixed16ToFloat(srcVtx->pos.x);
			outVtx->pos.y = fixed16ToFloat(srcVtx->pos.y);
			outVtx->pos.z = fixed16ToFloat(srcVtx->pos.z);

			outVtx->nrm.x = fixed16ToFloat(srcVtx->nrml.x);
			outVtx->nrm.y = fixed16ToFloat(srcVtx->nrml.y);
			outVtx->nrm.z = fixed16ToFloat(srcVtx->nrml.z);

			outVtx->uv.x = fixed16ToFloat(srcVtx->uv.x);
			outVtx->uv.z = fixed16ToFloat(srcVtx->uv.y);

			u8* outColor = (u8*)&outVtx->color;
			outColor[0] = srcVtx->color;
			outColor[1] = srcVtx->textureId >= 0 ? srcVtx->textureId & 0xff : 0xff;
			outColor[2] = srcVtx->textureId >= 0 ? (srcVtx->textureId >> 8) & 0xff : 0xff;
			outColor[3] = srcVtx->planeMode ? 0xff : 0x00;
		}

		*s_curIndexStart  = (s32)s_indexData.size();
		*s_curVertexStart = (s32)s_vertexData.size();
	}

	u32 getVertexKey(vec3* pos)
	{
		return u32(floor16(pos->x) + floor16(pos->y)*256 + floor16(pos->z)*65536);
	}

	bool isCompositeVtxEqual(const CompositeVertex* srcVtx, vec3* pos, vec2* uv, vec3* nrml, u8 color, u8 planeMode, s32 textureId)
	{
		if (srcVtx->pos.x != pos->x || srcVtx->pos.y != pos->y || srcVtx->pos.z != pos->z) { return false; }
		if (srcVtx->nrml.x != nrml->x || srcVtx->nrml.y != nrml->y || srcVtx->nrml.z != nrml->z) { return false; }
		if (srcVtx->uv.x != uv->x || srcVtx->uv.y != uv->y || srcVtx->textureId != textureId) { return false; }
		return srcVtx->color == color && srcVtx->planeMode == planeMode;
	}

	u32 getVertex(vec3* pos, vec2* uv, vec3* nrml, u8 color, u8 planeMode, s32 textureId)
	{
		// If the vertex already exists, then return it.
		const u32 key = getVertexKey(pos);
		bool keyFound = false;
		std::map<u32, std::vector<u32>>::iterator iBucket = s_modelVertexMap.find(key);
		if (iBucket != s_modelVertexMap.end())
		{
			keyFound = true;

			std::vector<u32>& vtxList = iBucket->second;
			const size_t count = vtxList.size();
			const u32* listIndices = vtxList.data();
			const CompositeVertex* listVtx = s_modelVertexList.data();
			for (size_t i = 0; i < count; i++)
			{
				const CompositeVertex* vtx = &listVtx[listIndices[i]];
				if (isCompositeVtxEqual(vtx, pos, uv, nrml, color, planeMode, textureId))
				{
					s_verticesMerged++;
					return vtx->index;
				}
			}
		}

		// Otherwise we need to add it.
		const u32 newId = (u32)s_modelVertexList.size();

		CompositeVertex newVtx;
		newVtx.pos   = *pos;
		newVtx.uv    = *uv;
		newVtx.nrml  = *nrml;
		newVtx.color = color;
		newVtx.planeMode = planeMode;
		newVtx.textureId = textureId;
		newVtx.index = newId;

		if (!keyFound)
		{
			std::vector<u32> newList;
			newList.push_back(newId);
			s_modelVertexMap[key] = newList;
		}
		else
		{
			s_modelVertexMap[key].push_back(newId);
		}
		s_modelVertexList.push_back(newVtx);

		return newId;
	}

	void addFlatTriangle(s32* indices, u8 color, vec2* uv, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &s_curModel->vertices[indices[0]];
		vec3* v1 = &s_curModel->vertices[indices[1]];
		vec3* v2 = &s_curModel->vertices[indices[2]];

		vec2 zero[3] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;
		vec3 nrmDir = { nrml->x - v1->x, nrml->y - v1->y, nrml->z - v1->z };

		s_indexData.push_back(getVertex(v0, &srcUV[0], &nrmDir, color, 0/*planeMode*/, textureId) + (*s_curVertexStart));
		s_indexData.push_back(getVertex(v1, &srcUV[1], &nrmDir, color, 0/*planeMode*/, textureId) + (*s_curVertexStart));
		s_indexData.push_back(getVertex(v2, &srcUV[2], &nrmDir, color, 0/*planeMode*/, textureId) + (*s_curVertexStart));
	}

	void addFlatQuad(s32* indices, u8 color, vec2* uv, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &s_curModel->vertices[indices[0]];
		vec3* v1 = &s_curModel->vertices[indices[1]];
		vec3* v2 = &s_curModel->vertices[indices[2]];
		vec3* v3 = &s_curModel->vertices[indices[3]];
		vec3 nrmDir = { nrml->x - v1->x, nrml->y - v1->y, nrml->z - v1->z };

		vec2 zero[4] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;

		u32 outIndices[4];
		outIndices[0] = getVertex(v0, &srcUV[0], &nrmDir, color, 0/*planeMode*/, textureId);
		outIndices[1] = getVertex(v1, &srcUV[1], &nrmDir, color, 0/*planeMode*/, textureId);
		outIndices[2] = getVertex(v2, &srcUV[2], &nrmDir, color, 0/*planeMode*/, textureId);
		outIndices[3] = getVertex(v3, &srcUV[3], &nrmDir, color, 0/*planeMode*/, textureId);

		s_indexData.push_back(outIndices[0] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[1] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[2] + (*s_curVertexStart));

		s_indexData.push_back(outIndices[0] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[2] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[3] + (*s_curVertexStart));
	}

	void addSmoothTriangle(s32* indices, u8 color, vec2* uv, s32 textureId)
	{
		vec3* v0 = &s_curModel->vertices[indices[0]];
		vec3* v1 = &s_curModel->vertices[indices[1]];
		vec3* v2 = &s_curModel->vertices[indices[2]];

		vec3* n0 = &s_curModel->vertexNormals[indices[0]];
		vec3* n1 = &s_curModel->vertexNormals[indices[1]];
		vec3* n2 = &s_curModel->vertexNormals[indices[2]];

		vec3 nDir0 = { n0->x - v0->x, n0->y - v0->y, n0->z - v0->z };
		vec3 nDir1 = { n1->x - v1->x, n1->y - v1->y, n1->z - v1->z };
		vec3 nDir2 = { n2->x - v2->x, n2->y - v2->y, n2->z - v2->z };

		vec2 zero[3] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;

		s_indexData.push_back(getVertex(v0, &srcUV[0], &nDir0, color, 0/*planeMode*/, textureId) + (*s_curVertexStart));
		s_indexData.push_back(getVertex(v1, &srcUV[1], &nDir1, color, 0/*planeMode*/, textureId) + (*s_curVertexStart));
		s_indexData.push_back(getVertex(v2, &srcUV[2], &nDir2, color, 0/*planeMode*/, textureId) + (*s_curVertexStart));
	}

	void addSmoothQuad(s32* indices, u8 color, vec2* uv, s32 textureId)
	{
		vec3* v0 = &s_curModel->vertices[indices[0]];
		vec3* v1 = &s_curModel->vertices[indices[1]];
		vec3* v2 = &s_curModel->vertices[indices[2]];
		vec3* v3 = &s_curModel->vertices[indices[3]];

		vec3* n0 = &s_curModel->vertexNormals[indices[0]];
		vec3* n1 = &s_curModel->vertexNormals[indices[1]];
		vec3* n2 = &s_curModel->vertexNormals[indices[2]];
		vec3* n3 = &s_curModel->vertexNormals[indices[3]];

		vec3 nDir0 = { n0->x - v0->x, n0->y - v0->y, n0->z - v0->z };
		vec3 nDir1 = { n1->x - v1->x, n1->y - v1->y, n1->z - v1->z };
		vec3 nDir2 = { n2->x - v2->x, n2->y - v2->y, n2->z - v2->z };
		vec3 nDir3 = { n3->x - v3->x, n3->y - v3->y, n3->z - v3->z };

		vec2 zero[4] = { 0 };
		vec2* srcUV = (uv && textureId >= 0) ? uv : zero;

		u32 outIndices[4];
		outIndices[0] = getVertex(v0, &srcUV[0], &nDir0, color, 0/*planeMode*/, textureId);
		outIndices[1] = getVertex(v1, &srcUV[1], &nDir1, color, 0/*planeMode*/, textureId);
		outIndices[2] = getVertex(v2, &srcUV[2], &nDir2, color, 0/*planeMode*/, textureId);
		outIndices[3] = getVertex(v3, &srcUV[3], &nDir3, color, 0/*planeMode*/, textureId);

		s_indexData.push_back(outIndices[0] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[1] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[2] + (*s_curVertexStart));

		s_indexData.push_back(outIndices[0] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[2] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[3] + (*s_curVertexStart));
	}

	void addPlaneTriangle(s32* indices, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &s_curModel->vertices[indices[0]];
		vec3* v1 = &s_curModel->vertices[indices[1]];
		vec3* v2 = &s_curModel->vertices[indices[2]];
		vec2 uv = { 0 };

		const vec3 up = { 0,  ONE_16, 0 };
		const vec3 dn = { 0, -ONE_16, 0 };
		vec3 planeNrm = nrml->y - v1->y > 0 ? up : dn;

		s_indexData.push_back(getVertex(v0, &uv, &planeNrm, 255, 1/*planeMode*/, textureId) + (*s_curVertexStart));
		s_indexData.push_back(getVertex(v1, &uv, &planeNrm, 255, 1/*planeMode*/, textureId) + (*s_curVertexStart));
		s_indexData.push_back(getVertex(v2, &uv, &planeNrm, 255, 1/*planeMode*/, textureId) + (*s_curVertexStart));
	}

	void addPlaneQuad(s32* indices, vec3* nrml, s32 textureId)
	{
		vec3* v0 = &s_curModel->vertices[indices[0]];
		vec3* v1 = &s_curModel->vertices[indices[1]];
		vec3* v2 = &s_curModel->vertices[indices[2]];
		vec3* v3 = &s_curModel->vertices[indices[3]];
		vec2 uv = { 0 };

		const vec3 up = { 0,  ONE_16, 0 };
		const vec3 dn = { 0, -ONE_16, 0 };
		vec3 planeNrm = nrml->y - v1->y > 0 ? up : dn;

		u32 outIndices[4];
		outIndices[0] = getVertex(v0, &uv, &planeNrm, 255, 1/*planeMode*/, textureId);
		outIndices[1] = getVertex(v1, &uv, &planeNrm, 255, 1/*planeMode*/, textureId);
		outIndices[2] = getVertex(v2, &uv, &planeNrm, 255, 1/*planeMode*/, textureId);
		outIndices[3] = getVertex(v3, &uv, &planeNrm, 255, 1/*planeMode*/, textureId);

		s_indexData.push_back(outIndices[0] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[1] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[2] + (*s_curVertexStart));

		s_indexData.push_back(outIndices[0] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[2] + (*s_curVertexStart));
		s_indexData.push_back(outIndices[3] + (*s_curVertexStart));
	}

	void model_loadLevelModels()
	{
		s32 indexStart  = 0;
		s32 vertexStart = 0;
		s_vertexData.clear();
		s_indexData.clear();
		s_modelCount = 0;
		s_verticesMerged = 0;

		std::vector<JediModel*> modelList;
		TFE_Model_Jedi::getModelList(modelList);
		const size_t modelCount = modelList.size();
		JediModel** model = modelList.data();
		for (size_t i = 0; i < modelCount; i++)
		{
			if (model[i]->flags & MFLAG_DRAW_VERTICES)
			{
				buildModelDrawVertices(model[i], &indexStart, &vertexStart);
				continue;
			}

			// This model has solid polygons.
			if (!startModel(model[i], &indexStart, &vertexStart))
			{
				// Too many unique models!
				break;
			}
			for (s32 p = 0; p < model[i]->polygonCount; p++)
			{
				JmPolygon* poly = &model[i]->polygons[p];
				if (poly->texture && (poly->texture->flags & OPACITY_TRANS) && poly->shading == PSHADE_PLANE)
				{
					s_modelTrans = true;
				}

				switch (poly->shading)
				{
					case PSHADE_FLAT:
					{
						// Flat shaded polygon
						if (poly->vertexCount == 3)
						{
							addFlatTriangle(poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], -1);
						}
						else
						{
							addFlatQuad(poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], -1);
						}
					} break;
					case PSHADE_GOURAUD:
					{
						// Smooth shaded polygon
						if (poly->vertexCount == 3)
						{
							addSmoothTriangle(poly->indices, poly->color, poly->uv, -1);
						}
						else
						{
							addSmoothQuad(poly->indices, poly->color, poly->uv, -1);
						}
					} break;
					case PSHADE_TEXTURE:
					{
						// Flat shaded textured polygon
						if (poly->vertexCount == 3)
						{
							addFlatTriangle(poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], poly->texture->textureId);
						}
						else
						{
							addFlatQuad(poly->indices, poly->color, poly->uv, &model[i]->polygonNormals[p], poly->texture->textureId);
						}
					} break;
					case PSHADE_GOURAUD_TEXTURE:
					{
						// Smooth shaded textured polygon
						if (poly->vertexCount == 3)
						{
							addSmoothTriangle(poly->indices, poly->color, poly->uv, poly->texture->textureId);
						}
						else
						{
							addSmoothQuad(poly->indices, poly->color, poly->uv, poly->texture->textureId);
						}
					} break;
					case PSHADE_PLANE:
					{
						// "Plane" shaded textured polygon
						if (poly->vertexCount == 3)
						{
							addPlaneTriangle(poly->indices, &model[i]->polygonNormals[p], poly->texture->textureId);
						}
						else
						{
							addPlaneQuad(poly->indices, &model[i]->polygonNormals[p], poly->texture->textureId);
						}
					} break;
				};
			}
			endModel();
		}

		s_modelVertexBuffer.create((u32)s_vertexData.size(), sizeof(ModelVertex), (u32)c_modelAttrCount, c_modelAttrMapping, false, s_vertexData.data());
		s_modelIndexBuffer.create((u32)s_indexData.size(), sizeof(u32), false, s_indexData.data());
	}

	void model_drawListClear()
	{
		for (s32 i = 0; i < MGPU_SHADER_COUNT; i++)
		{
			s_modelDrawListCount[i] = 0;
		}
	}

	void model_drawListFinish()
	{
	}

	void model_add(JediModel* model, Vec3f posWS, fixed16_16* transform, f32 ambient, Vec2f floorOffset)
	{
		// Make sure the model has been assigned a GPU ID.
		if (model->drawId < 0)
		{
			return;
		}

		ModelGPU* modelGPU = &s_models[model->drawId];
		ModelDraw* drawList = s_modelDrawList[modelGPU->shader];

		s32* listCount = &s_modelDrawListCount[modelGPU->shader];
		if ((*listCount) >= MGPU_MAX_3DO_PER_PASS)
		{
			// Too many objects in a single pass!
			assert(0);
			return;
		}

		ModelDraw* drawItem = &drawList[*listCount];
		(*listCount)++;

		drawItem->modelId = model->drawId;
		drawItem->posWS = posWS;
		drawItem->ambient = ambient;
		drawItem->floorOffset = floorOffset;
		for (s32 i = 0; i < 9; i++)
		{
			drawItem->transform[i] = fixed16ToFloat(transform[i]);
		}
	}

	void model_drawList()
	{
		s_modelVertexBuffer.bind();
		s_modelIndexBuffer.bind();
		Vec4f lightData = { f32(s_worldAmbient), 0.0f, 0.0f, 0.0f };

		for (s32 s = 0; s < MGPU_SHADER_COUNT; s++)
		{
			ModelDraw* drawList = s_modelDrawList[s];
			s32 listCount = s_modelDrawListCount[s];
			if (!listCount) { continue; }

			Shader* shader = &s_modelShaders[s];
			shader->bind();

			// Camera and lighting.
			shader->setVariable(s_mgpu_cameraPosId[s],   SVT_VEC3,   s_cameraPos.m);
			shader->setVariable(s_mgpu_cameraViewId[s],  SVT_MAT3x3, s_cameraMtx.data);
			shader->setVariable(s_mgpu_cameraProjId[s],  SVT_MAT4x4, s_cameraProj.data);
			shader->setVariable(s_mgpu_cameraDirId[s],   SVT_VEC3,   s_cameraDir.m);
			shader->setVariable(s_mgpu_cameraRightId[s], SVT_VEC3,   s_cameraRight.m);
			
			ModelDraw* drawItem = drawList;
			for (s32 i = 0; i < listCount; i++, drawItem++)
			{
				const ModelGPU* model = &s_models[drawItem->modelId];

				shader->setVariable(s_mgpu_modelPosId[s], SVT_VEC3,   drawItem->posWS.m);
				shader->setVariable(s_mgpu_modelMtxId[s], SVT_MAT3x3, drawItem->transform);

				lightData.y = min(drawItem->ambient, 31.0f) + (s_cameraLightSource ? 64.0f : 0.0f);
				lightData.z = drawItem->floorOffset.x;
				lightData.w = drawItem->floorOffset.z;
				shader->setVariable(s_mgpu_lightDataId[s], SVT_VEC4, lightData.m);

				TFE_RenderBackend::drawIndexedTriangles(model->polyCount, sizeof(u32), model->indexStart);
			}
		}

		// Cleanup
		Shader::unbind();
		s_modelVertexBuffer.unbind();
		s_modelIndexBuffer.unbind();
		TextureGpu::clear(0);
		TextureGpu::clear(1);
		TextureGpu::clear(2);
	}
}
