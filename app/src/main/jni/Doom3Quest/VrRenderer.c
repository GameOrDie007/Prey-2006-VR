#include "VrBase.h"
#include "VrInput.h"
#include "VrRenderer.h"
#include "PcvrInstrument.h"

#include <assert.h>
#include <string.h>

XrFovf fov;

// PCVR: which locate the current projections[] came from, and the copy the game
// took for its tic. The projection layer must declare the pose the world was
// drawn from, not a newer one - see PCVR_NoteLocate in win_pcvr.cpp for the
// measurement that made this necessary.
static long		vr_locateGen = 0;
static XrView	vr_gameViews[ 4 ];
static long		vr_gameGen = -1;

// PCVR: the same three, handed off with the frame instead of read live.
//
// vr_gameViews above is written by the MAIN thread when the tic reads the head
// pose, and read by the BACKEND when it declares the projection layer. With a
// render thread those are a frame apart, so by the time the backend declares
// the pose, the main thread has already overwritten it with the NEXT frame's -
// and the layer says the picture was drawn from somewhere it was not.
//
// That is the error the compositor reprojects against, so it is not corrected;
// it is applied. It is worth one frame of head rotation, on every frame, and
// only while the head is moving. Which is the complaint.
//
// So the pose travels with the frame: the main thread publishes at the handoff
// in RenderCommands, and the backend reads only what was published.
static XrView	vr_flightViews[ 4 ];
static long		vr_flightGen = -1;

// PCVR: the display time each pose was predicted FOR, so a frame can say how
// old the picture it is submitting actually is. See PCVR_NotePoseAge.
static XrTime	vr_locateTime = 0;
static XrTime	vr_gameLocateTime = 0;
static XrTime	vr_flightLocateTime = 0;
static XrTime	vr_lastSubmitTime = 0;
XrPosef pose[ovrMaxNumEyes];
XrView* projections;
bool initialized = false;
bool stageBoundsDirty = true;
bool stageSupported = false;
int vrConfig[VR_CONFIG_MAX] = {0};
float vrConfigFloat[VR_CONFIG_FLOAT_MAX] = {0};
PFN_xrGetDisplayRefreshRateFB pfnGetDisplayRefreshRate = NULL;
PFN_xrRequestDisplayRefreshRateFB pfnRequestDisplayRefreshRate = NULL;

XrPassthroughFB passthrough = XR_NULL_HANDLE;
XrPassthroughLayerFB passthroughLayer = XR_NULL_HANDLE;
DECL_PFN(xrCreatePassthroughFB);
DECL_PFN(xrDestroyPassthroughFB);
DECL_PFN(xrPassthroughStartFB);
DECL_PFN(xrPassthroughPauseFB);
DECL_PFN(xrCreatePassthroughLayerFB);
DECL_PFN(xrDestroyPassthroughLayerFB);
DECL_PFN(xrPassthroughLayerPauseFB);
DECL_PFN(xrPassthroughLayerResumeFB);

void VR_UpdateStageBounds(ovrApp* pappState) {
	XrExtent2Df stageBounds = {0};

	XrResult result;
	OXR(result = xrGetReferenceSpaceBoundsRect(pappState->Session, XR_REFERENCE_SPACE_TYPE_STAGE, &stageBounds));
	if (result != XR_SUCCESS) {
		stageBounds.width = 1.0f;
		stageBounds.height = 1.0f;

		pappState->CurrentSpace = pappState->FakeStageSpace;
	}
}

void VR_GetResolution(engine_t* engine, int *pWidth, int *pHeight) {
	static int width = 0;
	static int height = 0;

	// PCVR: this whole block runs once per frame - GLimp_SetupFrame calls
	// Doom3Quest_GetScreenRes, which calls this with a live engine - and it logs
	// four lines each time. On Android that is logcat, which no player sees and
	// which does not block. Here ALOGV is printf, and printf on the render
	// backend thread is a file or console write in the middle of the frame.
	//
	// The values it reports cannot change between frames: they are enumerated
	// from the XR system, and a resolution change goes through VR_InitRenderer.
	// So the logging is emitted on the first pass and suppressed after, leaving
	// the enumeration itself untouched. Nothing but log volume differs, and the
	// first frame still prints everything it used to.
	static int logged = 0;
	int firstPass = !logged;
	logged = 1;

	if (engine) {
		// Enumerate the viewport configurations.
		uint32_t viewportConfigTypeCount = 0;
		OXR(xrEnumerateViewConfigurations(
				engine->appState.Instance, engine->appState.SystemId, 0, &viewportConfigTypeCount, NULL));

		XrViewConfigurationType* viewportConfigurationTypes =
				(XrViewConfigurationType*)malloc(viewportConfigTypeCount * sizeof(XrViewConfigurationType));

		OXR(xrEnumerateViewConfigurations(
				engine->appState.Instance,
				engine->appState.SystemId,
				viewportConfigTypeCount,
				&viewportConfigTypeCount,
				viewportConfigurationTypes));

		if (firstPass) ALOGV("Available Viewport Configuration Types: %d", viewportConfigTypeCount);

		for (uint32_t i = 0; i < viewportConfigTypeCount; i++) {
			const XrViewConfigurationType viewportConfigType = viewportConfigurationTypes[i];

			if (firstPass) ALOGV(
					"Viewport configuration type %d : %s",
					viewportConfigType,
					viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ? "Selected" : "");

			XrViewConfigurationProperties viewportConfig;
			viewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
			OXR(xrGetViewConfigurationProperties(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, &viewportConfig));
			if (firstPass) ALOGV(
					"FovMutable=%s ConfigurationType %d",
					viewportConfig.fovMutable ? "true" : "false",
					viewportConfig.viewConfigurationType);

			uint32_t viewCount;
			OXR(xrEnumerateViewConfigurationViews(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, 0, &viewCount, NULL));

			if (viewCount > 0) {
				XrViewConfigurationView* elements =
						(XrViewConfigurationView*)malloc(viewCount * sizeof(XrViewConfigurationView));

				for (uint32_t e = 0; e < viewCount; e++) {
					elements[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
					elements[e].next = NULL;
				}

				OXR(xrEnumerateViewConfigurationViews(
						engine->appState.Instance,
						engine->appState.SystemId,
						viewportConfigType,
						viewCount,
						&viewCount,
						elements));

				// Cache the view config properties for the selected config type.
				if (viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
					assert(viewCount == ovrMaxNumEyes);
					for (uint32_t e = 0; e < viewCount; e++) {
						engine->appState.ViewConfigurationView[e] = elements[e];
					}
				}

				free(elements);
			} else {
				if (firstPass) ALOGE("Empty viewport configuration type: %d", viewCount);
			}
		}

		free(viewportConfigurationTypes);

		*pWidth = width = engine->appState.ViewConfigurationView[0].recommendedImageRectWidth;
		*pHeight = height = engine->appState.ViewConfigurationView[0].recommendedImageRectHeight;
	} else {
		//use cached values
		*pWidth = width;
		*pHeight = height;
	}

	//Apply supersampling
	float supersampling = VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING);
	if (supersampling > 0) {
		*pWidth *= supersampling;
		*pHeight *= supersampling;
	}

	//Force square resolution
	if (VR_GetPlatformFlag(VR_PLATFORM_VIEWPORT_SQUARE)) {
		*pHeight = *pWidth;
	}
}

void VR_Recenter(engine_t* engine) {

	// Calculate recenter reference
	XrReferenceSpaceCreateInfo spaceCreateInfo = {0};
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.poseInReferenceSpace = XrPosef_Identity();
	if (engine->appState.CurrentSpace != XR_NULL_HANDLE) {
		XrSpaceLocation loc = {0};
		loc.type = XR_TYPE_SPACE_LOCATION;
		OXR(xrLocateSpace(engine->appState.HeadSpace, engine->appState.CurrentSpace, engine->predictedDisplayTime, &loc));
		XrVector3f hmdangles = XrQuaternionf_ToEulerAngles(loc.pose.orientation);

		VR_SetConfigFloat(VR_CONFIG_RECENTER_YAW, VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW) + hmdangles.y);
		float recenterYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW));
		spaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.y = sinf(recenterYaw / 2);
		spaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.w = cosf(recenterYaw / 2);
	}

	// Delete previous space instances
	if (engine->appState.StageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.StageSpace));
	}
	if (engine->appState.FakeStageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.FakeStageSpace));
	}

	// Create a default stage space to use if SPACE_TYPE_STAGE is not
	// supported, or calls to xrGetReferenceSpaceBoundsRect fail.
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceCreateInfo.poseInReferenceSpace = XrPosef_Identity();
	if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
		spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
	}
	OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.FakeStageSpace));
	ALOGV("Created fake stage space from local space with offset");
	engine->appState.CurrentSpace = engine->appState.FakeStageSpace;

	if (stageSupported) {
		spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		spaceCreateInfo.poseInReferenceSpace.position.y = 0.0;
		OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.StageSpace));
		ALOGV("Created stage space");
		if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
			engine->appState.CurrentSpace = engine->appState.StageSpace;
		}
	}

	// Update menu orientation
	VR_SetConfigFloat(VR_CONFIG_MENU_YAW, 0.0f);
	stageBoundsDirty = true;
}

void VR_InitRenderer( engine_t* engine, bool multiview ) {
	if (initialized) {
		VR_DestroyRenderer(engine);
	}

	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		INIT_PFN(xrCreatePassthroughFB);
		INIT_PFN(xrDestroyPassthroughFB);
		INIT_PFN(xrPassthroughStartFB);
		INIT_PFN(xrPassthroughPauseFB);
		INIT_PFN(xrCreatePassthroughLayerFB);
		INIT_PFN(xrDestroyPassthroughLayerFB);
		INIT_PFN(xrPassthroughLayerPauseFB);
		INIT_PFN(xrPassthroughLayerResumeFB);
	}

	int eyeW, eyeH;
	VR_GetResolution(engine, &eyeW, &eyeH);
	// PCVR: the size the swapchain is built at, in the freeze report. The hang
	// tracks this rather than anything else measured so far.
	PCVR_NoteEyeSize(eyeW, eyeH);
	VR_SetConfig(VR_CONFIG_VIEWPORT_WIDTH, eyeW);
	VR_SetConfig(VR_CONFIG_VIEWPORT_HEIGHT, eyeH);

	// Get the viewport configuration info for the chosen viewport configuration type.
	engine->appState.ViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
	OXR(xrGetViewConfigurationProperties(engine->appState.Instance, engine->appState.SystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &engine->appState.ViewportConfig));

	uint32_t numOutputSpaces = 0;
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, 0, &numOutputSpaces, NULL));
	XrReferenceSpaceType* referenceSpaces = (XrReferenceSpaceType*)malloc(numOutputSpaces * sizeof(XrReferenceSpaceType));
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, numOutputSpaces, &numOutputSpaces, referenceSpaces));

	for (uint32_t i = 0; i < numOutputSpaces; i++) {
		if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			stageSupported = true;
			break;
		}
	}

	free(referenceSpaces);

	if (engine->appState.CurrentSpace == XR_NULL_HANDLE) {
		VR_Recenter(engine);
	}

	projections = (XrView*)(malloc(ovrMaxNumEyes * sizeof(XrView)));
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		memset(&projections[eye], 0, sizeof(XrView));
		projections[eye].type = XR_TYPE_VIEW;
	}

	int msaa = VR_GetConfig(VR_CONFIG_VIEWPORT_MSAA);
	ovrRenderer_Create(engine->appState.Session, &engine->appState.Renderer, multiview, eyeW, eyeH, msaa > 0 ? msaa : 1);
#ifdef ANDROID
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_FOVEATION)) {
		ovrRenderer_SetFoveation(&engine->appState.Instance, &engine->appState.Session, &engine->appState.Renderer, XR_FOVEATION_LEVEL_HIGH_FB, 0, XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB);
	}
#endif

	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		XrPassthroughCreateInfoFB ptci = {XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
		XrResult result;
		OXR(result = xrCreatePassthroughFB(engine->appState.Session, &ptci, &passthrough));

		if (XR_SUCCEEDED(result)) {
			XrPassthroughLayerCreateInfoFB plci = {XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
			plci.passthrough = passthrough;
			plci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
			OXR(xrCreatePassthroughLayerFB(engine->appState.Session, &plci, &passthroughLayer));
		}

		OXR(xrPassthroughStartFB(passthrough));
	}
	initialized = true;
}

void VR_DestroyRenderer( engine_t* engine ) {
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		OXR(xrPassthroughLayerPauseFB(passthroughLayer));
		OXR(xrPassthroughPauseFB(passthrough));
		OXR(xrDestroyPassthroughFB(passthrough));
		passthrough = XR_NULL_HANDLE;
	}
	ovrRenderer_Destroy(&engine->appState.Renderer);
	free(projections);
	initialized = false;
}

// PCVR: has the renderer been built yet? The swapchain length is the thing
// that actually decides whether a frame can be started - VR_CONFIG_VIEWPORT_VALID
// is a "needs rebuilding" flag that FrameSetup clears on every supersampling
// change, so using it as a readiness test silently stopped the main thread from
// starting frames at all.
// PCVR: the game is about to build the player's view from the current pose.
// Keep a copy; VR_PublishGamePose hands it to the backend with the frame, so
// the projection layer declares the pose this picture really has.
void VR_SnapshotGamePose( void ) {
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		vr_gameViews[eye] = projections[eye];
	}
	vr_gameGen = vr_locateGen;
	vr_gameLocateTime = vr_locateTime;
	PCVR_NoteGamePoseGen();
}

// PCVR: called on the main thread from idRenderSystemLocal::RenderCommands, at
// the moment the command buffer is handed to the backend. Everything the
// backend needs to know about the pose this frame was built from goes with it.
void VR_PublishGamePose( void ) {
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		vr_flightViews[eye] = vr_gameViews[eye];
	}
	vr_flightGen = vr_gameGen;
	vr_flightLocateTime = vr_gameLocateTime;
}

bool VR_RendererReady( engine_t* engine ) {
	return engine->appState.Renderer.FrameBuffer.TextureSwapChainLength > 0;
}

bool VR_InitFrame( engine_t* engine ) {
	if (ovrApp_HandleXrEvents(&engine->appState)) {
		VR_Recenter(engine);
	}
	if (engine->appState.SessionActive == false) {
		return false;
	}

	if (stageBoundsDirty) {
		VR_UpdateStageBounds(&engine->appState);
		stageBoundsDirty = false;
	}

	// Update passthrough
	//
	// PCVR: the platform-flag guard is added. Every other passthrough call site
	// in this file already has it - lines 199, 251, 269 and 403 - and only this
	// one is reached with the extension absent. DECL_PFN initialises these
	// pointers to NULL and INIT_PFN only fills them inside
	// VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH), so on a runtime
	// without XR_FB_passthrough the else branch here is a call through a null
	// pointer on the very first frame. VirtualDesktopXR does not advertise the
	// extension; a Quest always does, which is why this has never fired for them.
	//
	// Inert on Android: the flag is set there, so both branches run exactly as
	// before. Written as a nested if rather than && so the else keeps belonging
	// to the config test, matching the shape at line 403.
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		if (VR_GetConfig(VR_CONFIG_PASSTHROUGH)) {
			OXR(xrPassthroughLayerResumeFB(passthroughLayer));
		} else {
			OXR(xrPassthroughLayerPauseFB(passthroughLayer));
		}
	}

	XrFrameState frameState = {0};
	frameState.type = XR_TYPE_FRAME_STATE;
	frameState.next = NULL;
	PCVR_Stage("xrWaitFrame - the compositor's pace");
	PCVR_CountXr(0);
	OXR(xrWaitFrame(engine->appState.Session, 0, &frameState));
	PCVR_CountXr(1);
	PCVR_Stage("xrWaitFrame returned");
	PCVR_NoteWaitReturn();
	engine->predictedDisplayTime = frameState.predictedDisplayTime;
	vr_locateTime = frameState.predictedDisplayTime;

	// Update HMD
	XrViewLocateInfo projectionInfo = {0};
	projectionInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	projectionInfo.viewConfigurationType = engine->appState.ViewportConfig.viewConfigurationType;
	projectionInfo.displayTime = frameState.predictedDisplayTime;
	projectionInfo.space = engine->appState.CurrentSpace;
	XrViewState viewState = {XR_TYPE_VIEW_STATE, NULL};
	uint32_t projectionCapacityInput = ovrMaxNumEyes;
	uint32_t projectionCountOutput = projectionCapacityInput;
	OXR(xrLocateViews(
			engine->appState.Session,
			&projectionInfo,
			&viewState,
			projectionCapacityInput,
			&projectionCountOutput,
			projections));

	// PCVR: every locate gets a number, so the game's copy of the pose and the
	// one handed to the compositor can be told apart. See PCVR_NotePoseGen.
	vr_locateGen++;
	PCVR_NoteLocate();

	// Update controllers
	IN_VRInputFrame(engine);

	float fovx = 0;
	float fovy = 0;
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		fovx += fabs(projections[eye].fov.angleDown - projections[eye].fov.angleUp) / 2.0f;
		fovy += fabs(projections[eye].fov.angleRight - projections[eye].fov.angleLeft) / 2.0f;
	}

	if (VR_GetPlatformFlag(VR_PLATFORM_VIEWPORT_UNCENTERED)) {
		fovy *= 1.1f;
	}
	// PCVR: the third passthrough site, and the one that was still live here.
	// The other two are guarded by the platform flag; this one was not, so
	// turning Mixed Reality on in the VR menu halved the field of view on a PC
	// - where there is no XR_FB_passthrough to show through it, and no way for
	// the player to connect the setting to what they are looking at.
	//
	// Inert on Android, where the flag is set and this runs exactly as before.
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		if (VR_GetConfig(VR_CONFIG_PASSTHROUGH)) {
			fovx /= 2.0f;
			fovy /= 2.0f;
		}
	}

	if (VR_GetPlatformFlag(VR_PLATFORM_VIEWPORT_SQUARE)) {
		VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVX, ToDegrees(fovy));
		fov.angleLeft = -fovy / 2.0f;
		fov.angleRight = fovy / 2.0f;
	} else {
	   VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVX, ToDegrees(fovx));
	   fov.angleLeft = -fovx / 2.0f;
	   fov.angleRight = fovx / 2.0f;
	}
	VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVY, ToDegrees(fovy));
	fov.angleDown = -fovy / 2.0f;
	fov.angleUp = fovy / 2.0f;

	ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer;

	// PCVR: TextureSwapChainLength is 0 until VR_InitRenderer has run, and
	// "% 0" on an integer is a divide by zero - a processor fault that takes
	// the process out on the spot. Their frame is only ever started after the
	// renderer exists, so this could not fire for them; on this port it did,
	// the moment the main thread started waiting for the very first frame.
	if (frameBuffer->TextureSwapChainLength > 0) {
		frameBuffer->TextureSwapChainIndex++;
		frameBuffer->TextureSwapChainIndex %= frameBuffer->TextureSwapChainLength;
	}

	return true;
}

void VR_BeginFrame( engine_t* engine ) {
	// Get the HMD pose, predicted for the middle of the time period during which
	// the new eye images will be displayed. The number of frames predicted ahead
	// depends on the pipeline depth of the engine and the synthesis rate.
	// The better the prediction, the less black will be pulled in at the edges.
	XrFrameBeginInfo beginFrameDesc = {0};
	beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
	beginFrameDesc.next = NULL;
	PCVR_Stage("xrBeginFrame");
	PCVR_CountXr(2);
	PCVR_XrIn();
	OXR(xrBeginFrame(engine->appState.Session, &beginFrameDesc));
	PCVR_XrOut(0);
	PCVR_NoteBeginFrameTime();

	// PCVR: the pose the GAME used, not the newest one.
	//
	// The projection layer declares "this image was rendered from here", and
	// the compositor reprojects on the difference between that and where the
	// head actually is. Hand it a pose the world was never drawn from and the
	// correction is wrong by exactly that much - which is the small judder on
	// head movement, measured at 126 frames in 2059 before this.
	//
	// vr_gameViews is whatever xrLocateViews last produced at the moment
	// Doom3Quest_getHMDOrientation read it for the tic. When the main thread
	// started the frame - the normal case - it is the same locate and this
	// changes nothing at all.
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		if (vr_flightGen >= 0) {
			memcpy(&pose[eye], &vr_flightViews[eye].pose, sizeof(XrPosef));
		} else {
			memcpy(&pose[eye], &projections[eye].pose, sizeof(XrPosef));
		}
	}

	PCVR_NotePoseGen(vr_flightGen >= 0 ? vr_flightGen : vr_locateGen);

	ovrFramebuffer_Acquire(&engine->appState.Renderer.FrameBuffer);
	ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer);
}

void VR_EndFrame( engine_t* engine ) {
	VR_BindFramebuffer(engine);

	// Show mouse cursor
	int vrMode = vrConfig[VR_CONFIG_MODE];
	bool screenMode = (vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN);
	if (screenMode && (vrConfig[VR_CONFIG_MOUSE_SIZE] > 0)) {
		int x = vrConfig[VR_CONFIG_MOUSE_X];
		int y = vrConfig[VR_CONFIG_MOUSE_Y];
		int sx = vrConfig[VR_CONFIG_MOUSE_SIZE];
		int sy = (int)((float)sx * VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT));
		ovrRenderer_MouseCursor(&engine->appState.Renderer, x, y, sx, sy);
	}

	ovrFramebuffer_Resolve(&engine->appState.Renderer.FrameBuffer);
	ovrFramebuffer_Release(&engine->appState.Renderer.FrameBuffer);
	ovrFramebuffer_SetNone();
}

void VR_FinishFrame( engine_t* engine ) {
	int layerCount = 0;
	ovrCompositorLayer_Union layerUnion[ovrMaxLayerCount];
	memset(layerUnion, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);

	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_PASSTHROUGH) && VR_GetConfig(VR_CONFIG_PASSTHROUGH)) {
		if (passthroughLayer != XR_NULL_HANDLE) {
			XrCompositionLayerPassthroughFB passthrough_layer = {XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
			passthrough_layer.layerHandle = passthroughLayer;
			passthrough_layer.flags = XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
			passthrough_layer.space = XR_NULL_HANDLE;
			layerUnion[layerCount++].Passthrough = passthrough_layer;
		}
	}

	int vrMode = vrConfig[VR_CONFIG_MODE];
	XrCompositionLayerProjectionView projection_layer_elements[2] = {0};
	if ((vrMode == VR_MODE_MONO_6DOF) || (vrMode == VR_MODE_STEREO_6DOF)) {
		VR_SetConfigFloat(VR_CONFIG_MENU_YAW, XrQuaternionf_ToEulerAngles(pose[0].orientation).y);

		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
			ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer;
			memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
			projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projection_layer_elements[eye].pose = pose[eye];
			projection_layer_elements[eye].fov = fov;

			memset(&projection_layer_elements[eye].subImage, 0, sizeof(XrSwapchainSubImage));
			projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
			projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
			projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
			projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
			projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
			projection_layer_elements[eye].subImage.imageArrayIndex = vrMode == VR_MODE_MONO_6DOF ? 0 : eye;
		}

		XrCompositionLayerProjection projection_layer = {0};
		projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projection_layer.space = engine->appState.CurrentSpace;
		projection_layer.viewCount = ovrMaxNumEyes;
		projection_layer.views = projection_layer_elements;

		layerUnion[layerCount++].Projection = projection_layer;
	} else if ((vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN)) {

		// Flat screen pose
		float distance = VR_GetConfigFloat(VR_CONFIG_CANVAS_DISTANCE);
		float menuYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_YAW));
		XrVector3f pos = {
				pose[0].position.x - sinf(menuYaw) * distance,
				pose[0].position.y - 1.5f,
				pose[0].position.z - cosf(menuYaw) * distance
		};
		XrVector3f yawAxis = {0, 1, 0};
		XrQuaternionf yaw = XrQuaternionf_CreateFromVectorAngle(yawAxis, menuYaw);

		// Setup the cylinder layer
		XrCompositionLayerCylinderKHR cylinder_layer = {0};
		cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
		cylinder_layer.space = engine->appState.CurrentSpace;
		memset(&cylinder_layer.subImage, 0, sizeof(XrSwapchainSubImage));
		cylinder_layer.subImage.imageRect.offset.x = 0;
		cylinder_layer.subImage.imageRect.offset.y = 0;
		cylinder_layer.subImage.imageRect.extent.width = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Width;
		cylinder_layer.subImage.imageRect.extent.height = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Height;
		cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer.ColorSwapChain.Handle;
		cylinder_layer.subImage.imageArrayIndex = 0;
		cylinder_layer.pose.orientation = yaw;
		cylinder_layer.pose.position = pos;
		cylinder_layer.radius = 12.0f;
		cylinder_layer.centralAngle = (float)(M_PI * 0.5);
		cylinder_layer.aspectRatio = VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);

#if defined( _WIN32 )
		// PCVR: a runtime without XR_KHR_composition_layer_cylinder cannot be
		// handed a cylinder layer at all, so the same image goes up as a quad.
		// Quad layers are core OpenXR 1.0 and need nothing enabled.
		//
		// Placement is derived from the cylinder it replaces rather than
		// invented: their pose puts the cylinder's centre `distance` in front
		// of the viewer along menuYaw, and the textured arc is centred on that
		// pose's -Z at `radius`, so the arc sits (radius + distance) ahead. The
		// visible width of a cylinder layer is radius * centralAngle and its
		// height is that width over aspectRatio, which is what the quad's size
		// is set to.
		//
		// UNTESTED IN A HEADSET. On VirtualDesktopXR - the runtime this port
		// targets - the cylinder extension is present and this branch never
		// runs. It exists so the port starts under SteamVR, which does not
		// advertise the extension, and that is what makes a headset-free test
		// run possible at all. If anyone ever plays it on SteamVR for real, the
		// framing here is the first thing to check.
		if (!VR_HasCylinderLayer()) {
			XrCompositionLayerQuad quad_layer = {0};
			quad_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
			quad_layer.space = engine->appState.CurrentSpace;
			quad_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			quad_layer.subImage = cylinder_layer.subImage;

			float ahead = cylinder_layer.radius + distance;
			quad_layer.pose.orientation = yaw;
			quad_layer.pose.position.x = pose[0].position.x - sinf(menuYaw) * ahead;
			quad_layer.pose.position.y = pos.y;
			quad_layer.pose.position.z = pose[0].position.z - cosf(menuYaw) * ahead;

			float quadWidth = cylinder_layer.radius * cylinder_layer.centralAngle;
			float aspect = cylinder_layer.aspectRatio;
			quad_layer.size.width = quadWidth;
			quad_layer.size.height = ( aspect > 0.0f ) ? ( quadWidth / aspect ) : quadWidth;

			layerUnion[layerCount++].Quad = quad_layer;
		} else
#endif
		// Build the cylinder layer
		if (vrMode == VR_MODE_MONO_SCREEN) {
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
		} else {
#if defined( _WIN32 )
			// PCVR: VirtualDesktopXR will not accept the per-eye cylinder pair in
			// the #else below. xrEndFrame returns XR_ERROR_RUNTIME_FAILURE for it
			// on every single frame, so nothing is ever presented and the headset
			// keeps displaying the last frame that did get through - which is a
			// loading screen from the first few frames of startup. Measured over
			// a 30-second run: 2036 failures in 2036 frames with the pair, 1 with
			// this single layer (the one before the session was running).
			//
			// None of that was visible until PREYVR_XR_CHECKS was added: OXR() is
			// _DEBUG-only upstream, so in a Release build every OpenXR call was
			// unchecked and a total failure to present looked exactly like
			// success.
			//
			// It is the same picture either way. Their two layers differ only in
			// eyeVisibility - both name the same swapchain, the same pose, and
			// the same subImage.imageArrayIndex of 0, since the RIGHT push
			// re-assigns the index that was already 0. Both eyes are therefore
			// shown array layer 0 regardless, and one XR_EYE_VISIBILITY_BOTH
			// layer carrying that image is equivalent. No stereo is lost here
			// that their own code was delivering.
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
#else
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
			cylinder_layer.subImage.imageArrayIndex = 0;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
#endif
		}
	} else {
		assert(false);
	}

	// Compose the layers for this frame.
	const XrCompositionLayerBaseHeader* layers[ovrMaxLayerCount] = {0};
	for (int i = 0; i < layerCount; i++) {
		layers[i] = (const XrCompositionLayerBaseHeader*)&layerUnion[i];
	}

	// PCVR: the judder measurement, taken where the frame leaves us.
	//
	// The picture was drawn from a pose predicted for vr_flightLocateTime. We are
	// telling the compositor the frame is for engine->predictedDisplayTime. The
	// difference is how stale the image is, in milliseconds, and it is the one
	// number that describes head-tracking feel without a head to move.
	//
	// A CONSTANT age is latency and does not judder - the compositor reprojects
	// it away. An age that changes frame to frame is judder, because the world
	// then appears to move at a rate that varies with nothing the player did.
	// So the report prints the spread, not the mean.
	// Only the world layer. A menu or a loading screen starts its frame from
	// finishEyeBuffer instead, so the pose and the submit come from the same
	// locate and the age is a structural 0 that means nothing - it was 79% of
	// the samples and it buried the world frames underneath it.
	if ( vr_flightLocateTime > 0 &&
			( vrMode == VR_MODE_STEREO_6DOF || vrMode == VR_MODE_MONO_6DOF ) ) {
		PCVR_NotePoseAge( (int)( ( engine->predictedDisplayTime - vr_flightLocateTime ) / 1000000 ) );
	}
	if ( vr_lastSubmitTime > 0 ) {
		PCVR_NoteSubmitInterval( (int)( ( engine->predictedDisplayTime - vr_lastSubmitTime ) / 1000000 ) );
	}
	vr_lastSubmitTime = engine->predictedDisplayTime;

	XrFrameEndInfo endFrameInfo = {0};
	endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
	endFrameInfo.displayTime = engine->predictedDisplayTime;
	endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endFrameInfo.layerCount = layerCount;
	endFrameInfo.layers = layers;
	PCVR_NoteSubmit();
	PCVR_Stage("xrEndFrame - handing the frame to the compositor");
	PCVR_CountXr(3);
	PCVR_XrIn();
	OXR(xrEndFrame(engine->appState.Session, &endFrameInfo));
	PCVR_XrOut(4);
	PCVR_Stage("xrEndFrame returned");

	if (VR_GetConfig(VR_CONFIG_NEED_RECENTER)) {
		VR_SetConfig(VR_CONFIG_NEED_RECENTER, false);
		VR_Recenter(engine);
	}
}

int VR_GetConfig(enum VRConfig config ) {
	return vrConfig[config];
}

void VR_SetConfig(enum VRConfig config, int value) {
	vrConfig[config] = value;
}

float VR_GetConfigFloat(enum VRConfigFloat config) {
	return vrConfigFloat[config];
}

void VR_SetConfigFloat(enum VRConfigFloat config, float value) {
	vrConfigFloat[config] = value;
}

void VR_BindFramebuffer(engine_t *engine) {
	if (!initialized) return;
	ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer);
}

XrPosef VR_GetView(int eye) {
	return projections[eye].pose;
}

int VR_GetRefreshRate() {
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_REFRESH)) {
		if (!pfnGetDisplayRefreshRate) {
			OXR(xrGetInstanceProcAddr(
					VR_GetEngine()->appState.Instance,
					"xrGetDisplayRefreshRateFB",
					(PFN_xrVoidFunction*)(&pfnGetDisplayRefreshRate)));
		}

		float currentDisplayRefreshRate = 0.0f;
		OXR(pfnGetDisplayRefreshRate(VR_GetEngine()->appState.Session, &currentDisplayRefreshRate));
		return (int)currentDisplayRefreshRate;
	}
	return 72;
}

void VR_SetRefreshRate(int refresh) {
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_REFRESH)) {
		if (!pfnRequestDisplayRefreshRate) {
			OXR(xrGetInstanceProcAddr(
					VR_GetEngine()->appState.Instance,
					"xrRequestDisplayRefreshRateFB",
					(PFN_xrVoidFunction*)(&pfnRequestDisplayRefreshRate)));
		}
		OXR(pfnRequestDisplayRefreshRate(VR_GetEngine()->appState.Session, 72.0f));
		OXR(pfnRequestDisplayRefreshRate(VR_GetEngine()->appState.Session, (float)refresh));
	}
}