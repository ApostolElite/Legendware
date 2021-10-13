//#include <sdk/misc/ClientClass.hpp>
#include "esp_preview.h"


auto misc_interface::get_flex_create_fn()->CreateClientClassFn {
	auto clazz = CInterfaces::Get().Client->GetAllClasses();
	while (fnv::hash_runtime(clazz->m_pNetworkName) != FNV("CBaseFlex"))//CBaseFlex
		clazz = clazz->m_pNext;
	return clazz->m_pCreateFn;
}

auto misc_interface::make_flex(int entry, int serial)->CBaseEntity* 
{
	static auto flex_create_fn = get_flex_create_fn();
	if (flex_create_fn) {
		flex_create_fn(entry, serial);
		const auto flex = static_cast<CBaseAttributableItem*>(CInterfaces::Get().EntityList->GetClientEntity(entry));
		flex->InitializeAsClientEntity("models/player/custom_player/legacy/ctm_sas_varianta.mdl", RenderGroup_t::RENDER_GROUP_OPAQUE_ENTITY);
		flex->AddEffects(0x20);
		auto mod = CInterfaces::Get().ModelInfo->GetModel(flex->GetModelIndex());
		if (mod)
			calculate_frame_distance(mod);
		return flex;
	}
	return nullptr;
}

auto misc_interface::calculate_frame_distance(const model_t* model) -> void {
	Vector vecMin, vecMax;
	CInterfaces::Get().ModelInfo->GetModelRenderBounds(model, vecMin, vecMax);
	Vector vecCenter = (vecMax + vecMin) * 0.5f;
	vecMin -= vecCenter;
	vecMax -= vecCenter;

	// Get the bounds points and transform them by the desired model panel rotation.
	Vector aBoundsPoints[8];
	aBoundsPoints[0].Init(vecMax.x, vecMax.y, vecMax.z);
	aBoundsPoints[1].Init(vecMin.x, vecMax.y, vecMax.z);
	aBoundsPoints[2].Init(vecMax.x, vecMin.y, vecMax.z);
	aBoundsPoints[3].Init(vecMin.x, vecMin.y, vecMax.z);
	aBoundsPoints[4].Init(vecMax.x, vecMax.y, vecMin.z);
	aBoundsPoints[5].Init(vecMin.x, vecMax.y, vecMin.z);
	aBoundsPoints[6].Init(vecMax.x, vecMin.y, vecMin.z);
	aBoundsPoints[7].Init(vecMin.x, vecMin.y, vecMin.z);

	// Translated center point (offset from camera center).
	Vector vecTranslateCenter = -vecCenter;

	// Build the rotation matrix.
	QAngle angPanelAngles(0, 0, 0);
	matrix3x4_t matRotation;
	math::angle_matrix(angPanelAngles, matRotation);

	Vector aXFormPoints[8];
	for (int iPoint = 0; iPoint < 8; ++iPoint)
	{
		math::vector_transform(aBoundsPoints[iPoint], matRotation, aXFormPoints[iPoint]);
	}

	Vector vecXFormCenter;
	math::vector_transform(-vecTranslateCenter, matRotation, vecXFormCenter);

	int w = 1000, h = 1000;
	float flW = (float)w;
	float flH = (float)h;

	float flFOVx = DEG2RAD(54 * 0.5f);
	auto fovx = flFOVx;
	if (fovx < 1 || fovx > 179)
		fovx = 90;
	float val = atan(tan(DEG2RAD(fovx) * 0.5f) / flW / flH);
	float flFOVy = RAD2DEG(val) * 2.0f;

	flFOVy = DEG2RAD(flFOVy);

	float flTanFOVx = tan(flFOVx);
	float flTanFOVy = tan(flFOVy);

	// Find the max value of x, y, or z
	float flDist = 0.0f;
	for (int iPoint = 0; iPoint < 8; ++iPoint)
	{
		float flDistZ = fabs(aXFormPoints[iPoint].z / flTanFOVy - aXFormPoints[iPoint].x);
		float flDistY = fabs(aXFormPoints[iPoint].y / flTanFOVx - aXFormPoints[iPoint].x);
		float flTestDist = std::max(flDistZ, flDistY);
		flDist = std::max(flDist, flTestDist);
	}

	// Scale the object down by 10%.
	flDist *= 1.10f;

	// Add the framing offset.
	vecXFormCenter += Vector(110, 5, 5);

	m_vecOriginOffset.x = flDist - vecXFormCenter.x;
	m_vecOriginOffset.y = -vecXFormCenter.y;
	m_vecOriginOffset.z = -vecXFormCenter.z;

	// Screen space points.
	Vector2D aScreenPoints[8];
	Vector aCameraPoints[8];
	for (int iPoint = 0; iPoint < 8; ++iPoint)
	{
		aCameraPoints[iPoint] = aXFormPoints[iPoint];
		aCameraPoints[iPoint].x += flDist;

		aScreenPoints[iPoint].x = aCameraPoints[iPoint].y / (flTanFOVx * aCameraPoints[iPoint].x);
		aScreenPoints[iPoint].y = aCameraPoints[iPoint].z / (flTanFOVy * aCameraPoints[iPoint].x);

		aScreenPoints[iPoint].x = (aScreenPoints[iPoint].x * 0.5f + 0.5f) * flW;
		aScreenPoints[iPoint].y = (aScreenPoints[iPoint].y * 0.5f + 0.5f) * flH;
	}

	// Find the min/max and center of the 2D bounding box of the object.
	Vector2D vecScreenMin(99999.0f, 99999.0f), vecScreenMax(-99999.0f, -99999.0f);
	for (int iPoint = 0; iPoint < 8; ++iPoint)
	{
		vecScreenMin.x = std::min(vecScreenMin.x, aScreenPoints[iPoint].x);
		vecScreenMin.y = std::min(vecScreenMin.y, aScreenPoints[iPoint].y);
		vecScreenMax.x = std::max(vecScreenMax.x, aScreenPoints[iPoint].x);
		vecScreenMax.y = std::max(vecScreenMax.y, aScreenPoints[iPoint].y);
	}

	vecScreenMin.x = clamp(vecScreenMin.x, 0.0f, flW);
	vecScreenMin.y = clamp(vecScreenMin.y, 0.0f, flH);
	vecScreenMax.x = clamp(vecScreenMax.x, 0.0f, flW);
	vecScreenMax.y = clamp(vecScreenMax.y, 0.0f, flH);

	// Offset the view port based on the calculated model 2D center and the center of the viewport.
	Vector2D vecScreenCenter = (vecScreenMax + vecScreenMin) * 0.5f;
	m_vecViewportOffset.x = -((flW * 0.5f) - vecScreenCenter.x);
	m_vecViewportOffset.y = -((flH * 0.5f) - vecScreenCenter.y);
}

auto misc_interface::paint_model() -> void {
	/*if (!LocalPlayer)
		return;*/
	static auto set_abs_origin_addr = utils::memory::find_pattern("client_panorama.dll", "55 8B EC 83 E4 F8 51 53 56 57 8B F1", 0);
	const auto set_abs_origin_fn = reinterpret_cast<void(__thiscall*)(void*, const Vector&)>(set_abs_origin_addr);
	typedef void(__thiscall* SetAbsAngleFn)(void*, const QAngle&);
	static SetAbsAngleFn SetAbsAngles = (SetAbsAngleFn)((DWORD)utils::memory::find_pattern("client_panorama.dll", "55 8B EC 83 E4 F8 83 EC 64 53 56 57 8B F1 E8", 0));

	if (!buffer)
		//CInterfaces::Get().MaterialSystem->ForceRenderTargAllocBegin();
		buffer = CInterfaces::Get().MaterialSystem->CreateRenderTargetFF("esp_preview");
	//CInterfaces::Get().MaterialSystem->ForceRenderTargAllocEnd();

	if (!flex_ent.Get()) {
		const auto entry = GLOBAL_INTERFACES.EntityList->GetHighestEntityIndex() + 1;
		const auto serial = rand() % 0x1000;
		make_flex(entry, serial);
		flex_ent = CHandle<CBaseEntity>(CBaseHandle(entry | serial << 16));
	}
	float flWidthRatio = ((float)1000.f / (float)1000.f) / (4.0f / 3.0f);

	Vector vecExtraModelOffset(15, 0, 0);
	set_abs_origin_fn(flex_ent.Get(), m_vecOriginOffset + vecExtraModelOffset);
	SetAbsAngles(flex_ent.Get(), QAngle(0, 0, 0));

	CMatRenderContextPtr pRenderContext(CInterfaces::Get().MaterialSystem);
	//pRenderContext->PushRenderTargetAndViewport();
	//pRenderContext->SetRenderTarget(buffer);

	int viewportX, viewportY, viewportWidth, viewportHeight;
	pRenderContext->GetViewport(viewportX, viewportY, viewportWidth, viewportHeight);

	CViewSetup view;
	memset(&view, 0, sizeof(view));
	view.x = /*x + */m_vecViewportOffset.x + viewportX; // we actually want to offset by the
	view.y =/* y + */m_vecViewportOffset.y + viewportY; // viewport origin here because Push3DView expects global coords below
	view.width = 1000;
	view.height = 1000;

	view.m_bOrtho = false;
	// scale the FOV for aspect ratios other than 4/3
	{
		float halfAngleRadians = 54 * (0.5f * M_PI / 180.0f);
		float t = tan(halfAngleRadians);
		t *= flWidthRatio;
		float retDegrees = (180.0f / M_PI) * atan(t);
		view.fov = retDegrees * 2.0f;
	}
	view.origin.Init(0, 0, 0);
	view.angles.Init();
	view.zNear = 7;
	view.zFar = 1000;
	static auto pCubemapTexture = CInterfaces::Get().MaterialSystem->FindTexture("editor/cubemap.hdr", NULL, true);
	//pRenderContext->SetRenderTarget(buffer);
	pRenderContext->BindLocalCubemap(pCubemapTexture);

	pRenderContext->SetLightingOrigin(0, 0, 0);
	//pRenderContext->SetAmbientLight(0.4, 0.4, 0.4);

	static Vector white[6] =
	{
		Vector(0.4, 0.4, 0.4),
		Vector(0.4, 0.4, 0.4),
		Vector(0.4, 0.4, 0.4),
		Vector(0.4, 0.4, 0.4),
		Vector(0.4, 0.4, 0.4),
		Vector(0.4, 0.4, 0.4),
	};

	/*static Vector4D whites[6] =
	{
		Vector4D(0.4, 0.4, 0.4, 0.4),
		Vector4D(0.4, 0.4, 0.4, 0.4),
		Vector4D(0.4, 0.4, 0.4, 0.4),
		Vector4D(0.4, 0.4, 0.4, 0.4),
		Vector4D(0.4, 0.4, 0.4, 0.4),
		Vector4D(0.4, 0.4, 0.4, 0.4),
	};
	pRenderContext->SetAmbientLightCube(whites);*/
	CInterfaces::Get().StudioRender->SetAmbientLightColors(white);
	CInterfaces::Get().StudioRender->SetLocalLights(0, NULL);

	Frustum dummyFrustum;
	CInterfaces::Get().RenderView->Push3DView(pRenderContext, view, 0, NULL, dummyFrustum);
	CInterfaces::Get().ModelRender->SuppressEngineLighting(true);

	float color[3] = { 1.0f, 1.0f, 1.0f };
	CInterfaces::Get().RenderView->SetColorModulation(color);
	CInterfaces::Get().RenderView->SetBlend(1.0f);

	flex_ent->DrawModel(1, 255);
	CInterfaces::Get().ModelRender->SuppressEngineLighting(false);
	CInterfaces::Get().RenderView->PopView(pRenderContext, dummyFrustum);
	//pRenderContext->PopRenderTargetAndViewport();
	pRenderContext->BindLocalCubemap(NULL);
	/*if (buffer && buffer->m_pTextureHandles)
		PizdaMenu::draw_list->AddRectangleUV(
			Vector(0, 0), Vector(1000, 1000), Color::White(),
			(LPDIRECT3DTEXTURE9)(buffer->m_pTextureHandles[0]->texture_ptr), UVTable(0, 0, 1, 1)
		);*/

		/*pRenderContext->DrawScreenSpaceRectangle(
			mirrorMaterial,
			mirrorX, mirrorY, mirrorWidth, mirrorHeight,
			0, 0, float(mirrorWidth), float(mirrorHeight),
			mirrorTexture->GetActualWidth(), mirrorTexture->GetActualHeight(),
			nullptr, 1, 1
		);*/
}