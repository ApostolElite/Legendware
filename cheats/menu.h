#pragma once
#include "../includes.hpp"

class c_menu : public singleton<c_menu> {
public:
	void draw( bool is_open );
	void menu_setup(ImGuiStyle &style);

	float dpi_scale = 1.f;

	ImFont* futura;
	ImFont* futura_large;
	ImFont* futura_small;

	ImFont* gotham;

	ImFont* ico_menu;
	ImFont* ico_bottom;

	float public_alpha;
	IDirect3DDevice9* device;
	float color_buffer[4] = { 1.f, 1.f, 1.f, 1.f };
private:
	struct {
		ImVec2 WindowPadding;
		float  WindowRounding;
		ImVec2 WindowMinSize;
		float  ChildRounding;
		float  PopupRounding;
		ImVec2 FramePadding;
		float  FrameRounding;
		ImVec2 ItemSpacing;
		ImVec2 ItemInnerSpacing;
		ImVec2 TouchExtraPadding;
		float  IndentSpacing;
		float  ColumnsMinSpacing;
		float  ScrollbarSize;
		float  ScrollbarRounding;
		float  GrabMinSize;
		float  GrabRounding;
		float  TabRounding;
		float  TabMinWidthForUnselectedCloseButton;
		ImVec2 DisplayWindowPadding;
		ImVec2 DisplaySafeAreaPadding;
		float  MouseCursorScale;
	} styles;

	bool update_dpi = false;
	bool update_scripts = false;
	void dpi_resize(float scale_factor, ImGuiStyle &style);

	int active_tab_index;
	ImGuiStyle style;
	int width = 850, height = 560;
	float child_height;

	float preview_alpha = 1.f;

	int active_tab;

	int rage_section;
	int legit_section;
	int visuals_section;
	int players_section;
	int misc_section;
	int settings_section;

	// we need to use 'int child' to seperate content in 2 childs
	void draw_ragebot(int child);
	void draw_tabs_ragebot();

	void draw_legit(int child);
	 
	void draw_visuals(int child);
	void draw_tabs_visuals();
	int current_profile = -1;

	void draw_players(int child);
	void draw_tabs_players();

	void draw_misc(int child);
	void draw_tabs_misc();

	void draw_settings(int child);

	void draw_lua(int child);
	void draw_radar(int child);
	void draw_player_list(int child);

	std::string preview = crypt_str("None");
};


//for render model pasted 
struct Texture_t
{
	std::byte			pad0[0xC];		// 0x0000
	IDirect3DTexture9* lpRawTexture;	// 0x000C
};


class ITexture
{
private:
	template <typename T, typename ... args_t>
	constexpr T CallVFunc(void* thisptr, std::size_t nIndex, args_t... argList)
	{
		using VirtualFns = T(__thiscall*)(void*, decltype(argList)...);
		return (*static_cast<VirtualFns**>(thisptr))[nIndex](thisptr, argList...);
	}
private:
	std::byte	pad0[0x50];		 // 0x0000
public:
	Texture_t** pTextureHandles; // 0x0050

	int GetActualWidth()
	{
		return CallVFunc<int>(this, 3);
	}

	int GetActualHeight()
	{
		return CallVFunc<int>(this, 4);
	}

	void IncrementReferenceCount()
	{
		CallVFunc<void>(this, 10);
	}

	void DecrementReferenceCount()
	{
		CallVFunc<void>(this, 11);
	}
};


class IRenderToRTHelperObject
{
public:
	virtual void Draw(const matrix3x4_t& matRootToWorld) = 0;
	virtual bool BoundingSphere(Vector& vecCenter, float& flRadius) = 0;
	virtual ITexture* GetEnvCubeMap() = 0;
};

class CCustomMaterialOwner
{
public:
	virtual ~CCustomMaterialOwner() { }
	virtual void SetCustomMaterial(void* pCustomMaterial, int nIndex = 0) = 0;	// either replaces and existing material (releasing the old one), or adds one to the vector
	virtual void OnCustomMaterialsUpdated() {}
	virtual void DuplicateCustomMaterialsToOther(CCustomMaterialOwner* pOther) const = 0;

public:
	CUtlVector<void*> vecCustomMaterials;
}; // Size: 0x0014

struct MDLSquenceLayer_t
{
	int		nSequenceIndex;
	float	flWeight;
};
enum MorphFormatFlags_t
{
	MORPH_POSITION = 0x0001,	// 3D
	MORPH_NORMAL = 0x0002,	// 3D
	MORPH_WRINKLE = 0x0004,	// 1D
	MORPH_SPEED = 0x0008,	// 1D
	MORPH_SIDE = 0x0010,	// 1D
};

#define MAXSTUDIOBONECTRLS			4
#define MAXSTUDIOPOSEPARAM			24
#define MAXSTUDIOSKINS				32		// total textures
#define MAXSTUDIOFLEXCTRL			96		// maximum number of flexcontrollers (input sliders)
#define MAXSTUDIOANIMBLOCKS			256
#define MAXSTUDIOFLEXDESC			1024	// maximum number of low level flexes (actual morph targets)

class CMDL
{
public:
	std::byte	pad[0x3C]; // 0x0000
	MDLHandle_t	hModelHandle; // 0x003C
	std::byte	pad0[0x8]; // 0x003E
	Color		Color; // 0x0046
	std::byte	pad1[0x2]; // 0x004A
	int			nSkin; // 0x004C
	int			nBody; // 0x0050
	int			nSequence; // 0x0054
	int			nLOD; // 0x0058
	float		flPlaybackRate; // 0x005C
	float		flTime; // 0x0060
	float		flCurrentAnimEndTime; // 0x0064
	float		arrFlexControls[MAXSTUDIOFLEXCTRL * 4]; // 0x0068
	Vector		vecViewTarget; // 0x0668
	bool		bWorldSpaceViewTarget; // 0x0674
	bool		bUseSequencePlaybackFPS; // 0x0675
	std::byte	pad2[0x2]; // 0x0676
	void* pProxyData; // 0x0678
	float		flTimeBasisAdjustment; // 0x067C
	std::byte	pad3[0x4]; // 0x0680 --isn't correct after this point iirc
	CUtlVector<int> arrSequenceFollowLoop; // 0x0684
	matrix3x4_t	matModelToWorld; // 0x0698
	bool		bRequestBoneMergeTakeover; // 0x06C8
}; // Size: 0x06C9 // 0x6D0?

class C_MergedMDL : public IRenderToRTHelperObject
{
public:
	virtual ~C_MergedMDL() { }
	virtual void SetMDL(MDLHandle_t hModelHandle, CCustomMaterialOwner* pCustomMaterialOwner = nullptr, void* pProxyData = nullptr) = 0;
	virtual void SetMDL(const char* szModelPath, CCustomMaterialOwner* pCustomMaterialOwner = nullptr, void* pProxyData = nullptr) = 0;

	void SetMergedMDL(const char* szModelPath, CCustomMaterialOwner* pCustomMaterialOwner = nullptr, void* pProxyData = nullptr);

	void SetupBonesForAttachmentQueries();

	void SetSequence(const int nSequence, const bool bUseSequencePlaybackFPS)
	{
		this->RootMDL.nSequence = nSequence;
		this->RootMDL.bUseSequencePlaybackFPS = bUseSequencePlaybackFPS;
		this->RootMDL.flTimeBasisAdjustment = this->RootMDL.flTime;
	}

	void SetSkin(int nSkin)
	{
		this->RootMDL.nSkin = nSkin;
	}

public:
	CMDL RootMDL; // 0x0000
	CUtlVector<CMDL> vecMergedModels; // 0x069C
	float arrPoseParameters[MAXSTUDIOPOSEPARAM]; // 0x06E9
	int	nSequenceLayers; // 0x0749
	MDLSquenceLayer_t sequenceLayers[8]; // 0x074D -> // end: 0x78D
}; // Expected Size: 0x075C




struct C_ChamsSettings
{
	int32_t m_iMainMaterial = 0;

	bool m_bRenderChams = false;
	bool m_aModifiers[4] = { false, false, false };

	Color m_aModifiersColors[4] = { { 255, 255, 255, 255 }, { 255, 255, 255, 255 }, { 255, 255, 255, 255 }, { 255, 255, 255, 255 } };
	Color m_Color = Color(255, 255, 255, 255);
};

class C_ViewSetup
{
public:
	int			iX;
	int			iUnscaledX;
	int			iY;
	int			iUnscaledY;
	int			iWidth;
	int			iUnscaledWidth;
	int			iHeight;
	int			iUnscaledHeight;
	bool		bOrtho;
	std::byte	pad0[0x8F];
	float		flFOV;
	float		flViewModelFOV;
	Vector		vecOrigin;
	QAngle		angView;
	float		flNearZ;
	float		flFarZ;
	float		flNearViewmodelZ;
	float		flFarViewmodelZ;
	float		flAspectRatio;
	float		flNearBlurDepth;
	float		flNearFocusDepth;
	float		flFarFocusDepth;
	float		flFarBlurDepth;
	float		flNearBlurRadius;
	float		flFarBlurRadius;
	float		flDoFQuality;
	int			nMotionBlurMode;
	float		flShutterTime;
	Vector		vecShutterOpenPosition;
	QAngle		vecShutterOpenAngles;
	Vector		vecShutterClosePosition;
	QAngle		vecShutterCloseAngles;
	float		flOffCenterTop;
	float		flOffCenterBottom;
	float		flOffCenterLeft;
	float		flOffCenterRight;
	bool		bOffCenter;
	bool		bRenderToSubrectOfLargerScreen;
	bool		bDoBloomAndToneMapping;
	bool		bDoDepthOfField;
	bool		bHDRTarget;
	bool		bDrawWorldNormal;
	bool		bCullFontFaces;
	bool		bCacheFullSceneState;
	bool		bCSMView;
};

class C_DrawModel
{
public:
	virtual void Instance();
	virtual ITexture* GetTexture() { return this->m_PreviewTexture; };
	virtual void SetChamsSettings(C_ChamsSettings Settings) { this->m_ChamsSettings = Settings; };
	virtual void SetGlow(int glow) { this->m_iGlow = glow; };
	virtual void SetGlowColor(Color glow) { this->m_GlowColor = glow; };
	virtual void Refresh() { this->m_PreviewTexture = nullptr; this->m_PreviewModel = nullptr; this->m_CubemapTexture = nullptr; };
private:
	ITexture* m_PreviewTexture = nullptr;
	ITexture* m_CubemapTexture = nullptr;
	C_MergedMDL* m_PreviewModel = nullptr;

	C_ViewSetup m_ViewSetup = { };
	C_ChamsSettings m_ChamsSettings = { };

	int32_t m_iGlow = 0;
	Color m_GlowColor = Color(255, 255, 255, 255);
};

inline C_DrawModel* g_DrawModel = new C_DrawModel();



template <class T>
class CBaseAutoPtr
{
public:
	CBaseAutoPtr() :
		pObject(nullptr) { }

	CBaseAutoPtr(T* pObject) :
		pObject(pObject) { }

	operator const void* () const
	{
		return pObject;
	}

	operator void* () const
	{
		return pObject;
	}

	operator const T* () const
	{
		return pObject;
	}

	operator const T* ()
	{
		return pObject;
	}

	operator T* ()
	{
		return pObject;
	}

	int	operator=(int i)
	{
		pObject = nullptr;
		return 0;
	}

	T* operator=(T* pSecondObject)
	{
		pObject = pSecondObject;
		return pSecondObject;
	}

	bool operator!() const
	{
		return (!pObject);
	}

	bool operator==(const void* pSecondObject) const
	{
		return (pObject == pSecondObject);
	}

	bool operator!=(const void* pSecondObject) const
	{
		return (pObject != pSecondObject);
	}

	bool operator==(T* pSecondObject) const
	{
		return operator==(static_cast<void*>(pSecondObject));
	}

	bool operator!=(T* pSecondObject) const
	{
		return operator!=(static_cast<void*>(pSecondObject));
	}

	bool operator==(const CBaseAutoPtr<T>& pSecondPtr) const
	{
		return operator==(static_cast<const void*>(pSecondPtr));
	}

	bool operator!=(const CBaseAutoPtr<T>& pSecondPtr) const
	{
		return operator!=(static_cast<const void*>(pSecondPtr));
	}

	T* operator->()
	{
		return pObject;
	}

	T& operator*()
	{
		return *pObject;
	}

	T** operator&()
	{
		return &pObject;
	}

	const T* operator->() const
	{
		return pObject;
	}

	const T& operator*() const
	{
		return *pObject;
	}

	T* const* operator&() const
	{
		return &pObject;
	}

	CBaseAutoPtr(const CBaseAutoPtr<T>& pSecondPtr) :
		pObject(pSecondPtr.pObject) { }

	void operator=(const CBaseAutoPtr<T>& pSecondPtr)
	{
		pObject = pSecondPtr.pObject;
	}

	T* pObject;
};


template <class T>
class CRefPtr : public CBaseAutoPtr<T>
{
	typedef CBaseAutoPtr<T> CBaseClass;
public:
	CRefPtr() { }

	CRefPtr(T* pInit)
		: CBaseClass(pInit) { }

	CRefPtr(const CRefPtr<T>& pRefPtr)
		: CBaseClass(pRefPtr) { }

	~CRefPtr()
	{
		if (CBaseClass::pObject != nullptr)
			CBaseClass::pObject->Release();
	}

	void operator=(const CRefPtr<T>& pSecondRefPtr)
	{
		CBaseClass::operator=(pSecondRefPtr);
	}

	int operator=(int i)
	{
		return CBaseClass::operator=(i);
	}

	T* operator=(T* pSecond)
	{
		return CBaseClass::operator=(pSecond);
	}

	operator bool() const
	{
		return !CBaseClass::operator!();
	}

	operator bool()
	{
		return !CBaseClass::operator!();
	}

	void SafeRelease()
	{
		if (CBaseClass::pObject != nullptr)
			CBaseClass::pObject->Release();

		CBaseClass::pObject = nullptr;
	}

	void AssignAddReference(T* pFrom)
	{
		if (pFrom != nullptr)
			pFrom->AddReference();

		SafeRelease();
		CBaseClass::pObject = pFrom;
	}

	void AddReferenceAssignTo(T*& pTo)
	{
		if (CBaseClass::pObject != nullptr)
			CBaseClass::pObject->AddReference();

		SafeRelease(pTo);
		pTo = CBaseClass::pObject;
	}
};

class IRefCounted
{
public:
	virtual int AddReference() = 0;
	virtual int Release() = 0;
};

struct LightDesc_t;
class IMatRenderContext : public IRefCounted
{
private:
	template <typename T, typename ... args_t>
	constexpr T CallVFunc(void* thisptr, std::size_t nIndex, args_t... argList)
	{
		using VirtualFnz = T(__thiscall*)(void*, decltype(argList)...);
		return (*static_cast<VirtualFnz**>(thisptr))[nIndex](thisptr, argList...);
	}
public:
	void BeginRender()
	{
		CallVFunc<void>(this, 2);
	}

	void EndRender()
	{
		CallVFunc<void>(this, 3);
	}

	void BindLocalCubemap(ITexture* pTexture)
	{
		CallVFunc<void>(this, 5, pTexture);
	}

	void SetRenderTarget(ITexture* pTexture)
	{
		CallVFunc<void>(this, 6, pTexture);
	}

	ITexture* GetRenderTarget()
	{
		return CallVFunc<ITexture*>(this, 7);
	}

	void ClearBuffers(bool bClearColor, bool bClearDepth, bool bClearStencil = false)
	{
		CallVFunc<void>(this, 12, bClearColor, bClearDepth, bClearStencil);
	}

	void SetLights(int nCount, const LightDesc_t* pLights)
	{
		CallVFunc<void>(this, 17, nCount, pLights);
	}

	void SetAmbientLightCube(Vector4D vecCube[6])
	{
		CallVFunc<void>(this, 18, vecCube);
	}

	void SetIntRenderingParameter(int a1, int a2)
	{
		return CallVFunc < void(__thiscall*)(void*, int, int) >(this, 126)(this, a1, a2);
	}

	void Viewport(int x, int y, int iWidth, int iHeight)
	{
		CallVFunc<void>(this, 40, x, y, iWidth, iHeight);
	}

	void GetViewport(int& x, int& y, int& iWidth, int& iHeight)
	{
		CallVFunc<void, int&, int&, int&, int&>(this, 41, x, y, iWidth, iHeight);
	}

	void ClearColor3ub(unsigned char r, unsigned char g, unsigned char b)
	{
		CallVFunc<void>(this, 78, r, g, b);
	}

	void ClearColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
	{
		CallVFunc<void>(this, 79, r, g, b, a);
	}

	void DrawScreenSpaceRectangle(IMaterial* pMaterial, int iDestX, int iDestY, int iWidth, int iHeight, float flTextureX0, float flTextureY0, float flTextureX1, float flTextureY1, int iTextureWidth, int iTextureHeight, void* pClientRenderable = nullptr, int nXDice = 1, int nYDice = 1)
	{
		CallVFunc<void>(this, 114, pMaterial, iDestX, iDestY, iWidth, iHeight, flTextureX0, flTextureY0, flTextureX1, flTextureY1, iTextureWidth, iTextureHeight, pClientRenderable, nXDice, nYDice);
	}

	void PushRenderTargetAndViewport()
	{
		CallVFunc<void>(this, 119);
	}

	void PopRenderTargetAndViewport()
	{
		CallVFunc<void>(this, 120);
	}

	void SetLightingOrigin(/*Vector vecLightingOrigin*/float x, float y, float z)
	{
		CallVFunc<void>(this, 158, x, y, z);
	}

};


class CMatRenderContextPtr : public CRefPtr<IMatRenderContext>
{
	typedef CRefPtr<IMatRenderContext> CBaseClass;
public:
	CMatRenderContextPtr() = default;

	CMatRenderContextPtr(IMatRenderContext* pInit) : CBaseClass(pInit)
	{
		if (CBaseClass::pObject != nullptr)
			CBaseClass::pObject->BeginRender();
	}

	CMatRenderContextPtr(IMaterialSystem* pFrom) : CBaseClass(pFrom->GetRenderContext())
	{
		if (CBaseClass::pObject != nullptr)
			CBaseClass::pObject->BeginRender();
	}

	~CMatRenderContextPtr()
	{
		if (CBaseClass::pObject != nullptr)
			CBaseClass::pObject->EndRender();
	}

	IMatRenderContext* operator=(IMatRenderContext* pSecondContext)
	{
		if (pSecondContext != nullptr)
			pSecondContext->BeginRender();

		return CBaseClass::operator=(pSecondContext);
	}

	void SafeRelease()
	{
		if (CBaseClass::pObject != nullptr)
			CBaseClass::pObject->EndRender();

		CBaseClass::SafeRelease();
	}

	void AssignAddReference(IMatRenderContext* pFrom)
	{
		if (CBaseClass::pObject)
			CBaseClass::pObject->EndRender();

		CBaseClass::AssignAddReference(pFrom);
		CBaseClass::pObject->BeginRender();
	}

	void GetFrom(IMaterialSystem* pFrom)
	{
		AssignAddReference(pFrom->GetRenderContext());
	}

private:
	CMatRenderContextPtr(const CMatRenderContextPtr& pRefPtr);
	void operator=(const CMatRenderContextPtr& pSecondRefPtr);
};






class C_StudioRender
{
private:
	template <typename T, typename ... args_t>
	constexpr T CallVFunc(void* thisptr, std::size_t nIndex, args_t... argList)
	{
		using VirtualFnxz = T(__thiscall*)(void*, decltype(argList)...);
		return (*static_cast<VirtualFnxz**>(thisptr))[nIndex](thisptr, argList...);
	}
public:
	void SetAmbientLightColors(const Vector4D* pAmbientOnlyColors)
	{
		CallVFunc<void>(this, 20, pAmbientOnlyColors);
	}
	void SetAmbientLightColors(const Vector* pAmbientOnlyColors)
	{
		CallVFunc<void>(this, 21, pAmbientOnlyColors);
	}
	void SetLocalLights(int nLights, const LightDesc_t* pLights)
	{
		CallVFunc<void>(this, 22, nLights, pLights);
	}

	void SetColorModulation(float const* arrColor)
	{
		CallVFunc<void>(this, 27, arrColor);
	}

	void SetAlphaModulation(float flAlpha)
	{
		CallVFunc<void>(this, 28, flAlpha);
	}

	void ForcedMaterialOverride(IMaterial* pMaterial, OverrideType_t nOverrideType = 0, int nOverrides = 0)
	{
		CallVFunc<void>(this, 33, pMaterial, nOverrideType, nOverrides);
	}
};

inline C_StudioRender* m_StudioRender = new C_StudioRender();