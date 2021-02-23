#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "ErosionMaker.h"
#include <stdio.h>
#include <algorithm>
#include <chrono>

#define RLIGHTS_IMPLEMENTATION
#include "rlights.h"

#define GLSL_VERSION            210
#define MAP_RESOLUTION			512 // width and height of heightmap
#define CLIP_SHADERS_COUNT		1 // number of shaders that use a clipPlane
#define TREE_TEXTURE_COUNT		19 // number of textures for a tree
#define TREE_COUNT				8190 // number of tree billboards

// defines a tree billboard
typedef struct
{
	Texture2D texture;
	Vector3 position;
	float scale = 1.0f;
	Color color = WHITE;
} TreeBillboard;
Shader treeShader; // shader used for tree billboards

// renders all 3d scene (include variants for above and below the surface)
void Render3DScene(Camera camera, Light lights[], std::vector<Model> models, std::vector<TreeBillboard> trees, int clipPlane);
// generates (or regenerates) all tree billboards
void GenerateTrees(ErosionMaker* erosionMaker, std::vector<float>* mapData, Texture2D* treeTextures, std::vector<TreeBillboard>* trees, bool generateNew);

// data used to store shaders that make use of clipPlanes
Shader clipShaders[CLIP_SHADERS_COUNT];
int clipShaderHeightLocs[CLIP_SHADERS_COUNT];
int clipShaderTypeLocs[CLIP_SHADERS_COUNT];
int AddClipShader(Shader shader)
{
	static int clipShadersCount = 0;
	clipShaders[clipShadersCount] = shader;
	clipShaderHeightLocs[clipShadersCount] = GetShaderLocation(shader, "cullHeight");
	clipShaderTypeLocs[clipShadersCount] = GetShaderLocation(shader, "cullType");
	clipShadersCount++;
	return clipShadersCount - 1;
}

// random float between two values
float static randomRange(float min, float max) {
	return min + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (max - min)));
}

int main(void)
{
	// Initialization
	//--------------------------------------------------------------------------------------
	const int screenWidth = 1280; // initial size of window
	const int screenHeight = 720;
	const float fboSize = 2.5f;
	int windowWidthBeforeFullscreen = screenWidth;
	int windowHeightBeforeFullscreen = screenWidth;
	bool windowSizeChanged = false; // set to true when switching to fullscreen

	const Vector2 displayResolutions[] =
	{
		{320, 180}, // 0
		{640, 360}, // 1
		{1280, 720}, // 2
		{1600, 900}, // 3
		{1920, 1080} // 4
	};
	int currentDisplayResolutionIndex = 2;

	bool useApplicationBuffer = false; // wether to use app buffer or not
	bool lockTo60FPS = false;

	float daytime = 0.2f; // range (0, 1) but is sent to shader as a range(-1, 1) normalized upon a unit sphere
	float dayspeed = 0.015f;
	bool dayrunning = true; // if day is animating
	float ambc[4] = { 0.22f, 0.17f, 0.41f, 0.2f }; // current ambient color & intensity

	Image ambientColorsImage = LoadImage("resources/ambientGradient.png");
	Vector4* ambientColors = GetImageDataNormalized(ambientColorsImage); // array of colors for ambient color through the day
	int ambientColorsNumber = ambientColorsImage.width; // length of array
	UnloadImage(ambientColorsImage);

	std::vector<TreeBillboard> noTrees; // keep empty
	std::vector<TreeBillboard> trees; // fill with tree data

	int totalDroplets = 0; // total amount of droplets simulated
	int dropletsSinceLastTreeRegen = 0; // used to regenerate trees after certain droplets have fallen

	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	InitWindow(screenWidth, screenHeight, "Terrain Erosion");

	Shader postProcessShader = LoadShader(0, "resources/shaders/postprocess.frag");
	// Create a RenderTexture2D to be used for render to texture
	RenderTexture2D applicationBuffer = LoadRenderTexture(GetScreenWidth(), GetScreenHeight()); // main FBO used for postprocessing
	RenderTexture2D reflectionBuffer = LoadRenderTexture(GetScreenWidth() / fboSize, GetScreenHeight() / fboSize); // FBO used for water reflection
	RenderTexture2D refractionBuffer = LoadRenderTexture(GetScreenWidth() / fboSize, GetScreenHeight() / fboSize); // FBO used for water refraction
	SetTextureFilter(reflectionBuffer.texture, FILTER_BILINEAR);
	SetTextureFilter(refractionBuffer.texture, FILTER_BILINEAR);
	/*SetTextureWrap(reflectionBuffer.texture, WRAP_CLAMP);
	SetTextureWrap(refractionBuffer.texture, WRAP_CLAMP);*/

	// Define our custom camera to look into our 3d world
	Camera camera = { {12.0f, 32.0f, 22.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f, 0 };
	SetCameraMode(camera, CAMERA_THIRD_PERSON);

	// Initialize the erosion maker
	ErosionMaker* erosionMaker = &ErosionMaker::GetInstance();

	Image initialHeightmapImage = GenImagePerlinNoise(MAP_RESOLUTION, MAP_RESOLUTION, 50, 50, 4.0f); // generate fractal perlin noise
	std::vector<float>* mapData = new std::vector<float>(MAP_RESOLUTION * MAP_RESOLUTION);
	// Extract pixels and put them in mapData
	Color* pixels = GetImageData(initialHeightmapImage);
	for (size_t i = 0; i < MAP_RESOLUTION * MAP_RESOLUTION; i++)
	{
		mapData->at(i) = pixels[i].r / 255.0f;
	}
	// Erode
	erosionMaker->Gradient(mapData, MAP_RESOLUTION, 0.5f, GradientType::SQUARE); // apply a centered gradient to smooth out border pixel (create island at center)
	erosionMaker->Remap(mapData, MAP_RESOLUTION); // flatten beaches
	erosionMaker->Erode(mapData, MAP_RESOLUTION, 0, true); // Erode (0 droplets for initialization)
	// Update pixels from mapData to texture
	for (size_t i = 0; i < MAP_RESOLUTION * MAP_RESOLUTION; i++)
	{
		int val = mapData->at(i) * 255;
		pixels[i].r = val;
		pixels[i].g = val;
		pixels[i].b = val;
		pixels[i].a = 255;
	}
	Image heightmapImage = LoadImageEx(pixels, MAP_RESOLUTION, MAP_RESOLUTION);
	Texture2D heightmapTexture = LoadTextureFromImage(heightmapImage); // Convert image to texture (VRAM)
	UnloadImage(heightmapImage); // Unload heightmap image from RAM, already uploaded to VRAM
	SetTextureFilter(heightmapTexture, FILTER_BILINEAR);
	SetTextureWrap(heightmapTexture, WRAP_CLAMP);
	GenTextureMipmaps(&heightmapTexture);


	// TERRAIN
	Mesh terrainMesh = GenMeshPlane(32, 32, 256, 256);// Generate terrain mesh (RAM and VRAM)
	Texture2D terrainGradient = LoadTexture("resources/terrainGradient.png"); // color ramp of terrain (rock and grass)
	//SetTextureFilter(terrainGradient, FILTER_BILINEAR);
	SetTextureWrap(terrainGradient, WRAP_CLAMP);
	GenTextureMipmaps(&terrainGradient);
	Model terrainModel = LoadModelFromMesh(terrainMesh); // Load model from generated mesh
	terrainModel.transform = MatrixTranslate(0, -1.2f, 0);
	terrainModel.materials[0].maps[0].texture = terrainGradient;
	terrainModel.materials[0].maps[2].texture = heightmapTexture;
	terrainModel.materials[0].shader = LoadShader("resources/shaders/terrain.vert", "resources/shaders/terrain.frag");
	// Get some shader loactions
	terrainModel.materials[0].shader.locs[LOC_MATRIX_MODEL] = GetShaderLocation(terrainModel.materials[0].shader, "matModel");
	terrainModel.materials[0].shader.locs[LOC_VECTOR_VIEW] = GetShaderLocation(terrainModel.materials[0].shader, "viewPos");
	int terrainDaytimeLoc = GetShaderLocation(terrainModel.materials[0].shader, "daytime");
	int cs = AddClipShader(terrainModel.materials[0].shader); // register as clip shader for automatization of clipPlanes
	float param10 = 0.0f;
	int param11 = 2;
	SetShaderValue(terrainModel.materials[0].shader, clipShaderHeightLocs[cs], &param10, UNIFORM_FLOAT);
	SetShaderValue(terrainModel.materials[0].shader, clipShaderTypeLocs[cs], &param11, UNIFORM_INT);
	// ambient light level
	int terrainAmbientLoc = GetShaderLocation(terrainModel.materials[0].shader, "ambient");
	SetShaderValue(terrainModel.materials[0].shader, terrainAmbientLoc, ambc, UNIFORM_VEC4);
	Texture2D rockNormalMap = LoadTexture("resources/rockNormalMap.png"); // normal map
	SetTextureFilter(rockNormalMap, FILTER_BILINEAR);
	GenTextureMipmaps(&rockNormalMap);
	terrainModel.materials[0].shader.locs[LOC_MAP_ROUGHNESS] = GetShaderLocation(terrainModel.materials[0].shader, "rockNormalMap");
	terrainModel.materials[0].maps[MAP_ROUGHNESS].texture = rockNormalMap;

	// OCEAN PLANE
	Mesh oceanMesh = GenMeshPlane(5120, 5120, 10, 10);
	Model oceanModel = LoadModelFromMesh(oceanMesh);
	Texture2D DUDVTex = LoadTexture("resources/waterDUDV.png");
	SetTextureFilter(DUDVTex, FILTER_BILINEAR);
	GenTextureMipmaps(&DUDVTex);
	oceanModel.transform = MatrixTranslate(0, 0, 0);
	oceanModel.materials[0].maps[0].texture = reflectionBuffer.texture; // uniform texture0
	oceanModel.materials[0].maps[1].texture = refractionBuffer.texture; // uniform texture1
	oceanModel.materials[0].maps[2].texture = DUDVTex; // uniform texture2
	oceanModel.materials[0].shader = LoadShader("resources/shaders/water.vert", "resources/shaders/water.frag");
	float waterMoveFactor = 0.0f;
	int waterMoveFactorLoc = GetShaderLocation(oceanModel.materials[0].shader, "moveFactor");
	oceanModel.materials[0].shader.locs[LOC_MATRIX_MODEL] = GetShaderLocation(oceanModel.materials[0].shader, "matModel");
	oceanModel.materials[0].shader.locs[LOC_VECTOR_VIEW] = GetShaderLocation(oceanModel.materials[0].shader, "viewPos");

	// OCEAN FLOOR
	Image whiteImage = GenImageColor(8, 8, BLACK);
	Texture2D whiteTexture = LoadTextureFromImage(whiteImage);
	UnloadImage(whiteImage);
	Mesh oceanFloorMesh = GenMeshPlane(5120, 5120, 10, 10);
	Model oceanFloorModel = LoadModelFromMesh(oceanFloorMesh);
	oceanFloorModel.transform = MatrixTranslate(0, -1.2f, 0);
	oceanFloorModel.materials[0].maps[0].texture = terrainGradient;
	oceanFloorModel.materials[0].maps[2].texture = whiteTexture;
	oceanFloorModel.materials[0].shader = terrainModel.materials[0].shader;

	// CLOUDS
	Texture2D cloudTexture = LoadTexture("resources/clouds.png");
	SetTextureFilter(cloudTexture, FILTER_BILINEAR);
	GenTextureMipmaps(&cloudTexture);
	Mesh cloudMesh = GenMeshPlane(51200, 51200, 10, 10);
	Model cloudModel = LoadModelFromMesh(cloudMesh);
	cloudModel.transform = MatrixTranslate(0, 1000.0f, 0);
	cloudModel.materials[0].shader = LoadShader("resources/shaders/cirrostratus.vert", "resources/shaders/cirrostratus.frag");
	float cloudMoveFactor = 0.0f;
	int cloudMoveFactorLoc = GetShaderLocation(cloudModel.materials[0].shader, "moveFactor");
	int cloudDaytimeLoc = GetShaderLocation(cloudModel.materials[0].shader, "daytime");
	cloudModel.materials[0].shader.locs[LOC_MATRIX_MODEL] = GetShaderLocation(cloudModel.materials[0].shader, "matModel");
	cloudModel.materials[0].shader.locs[LOC_VECTOR_VIEW] = GetShaderLocation(cloudModel.materials[0].shader, "viewPos");
	cloudModel.materials[0].maps[0].texture = cloudTexture;

	// SKYBOX
	Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
	Model skybox = LoadModelFromMesh(cube);
	skybox.materials[0].shader = LoadShader("resources/shaders/skybox.vert", "resources/shaders/skybox.frag");
	int skyboxDaytimeLoc = GetShaderLocation(skybox.materials[0].shader, "daytime");
	int skyboxDayrotationLoc = GetShaderLocation(skybox.materials[0].shader, "dayrotation");
	float skyboxMoveFactor = 0.0f;
	int skyboxMoveFactorLoc = GetShaderLocation(skybox.materials[0].shader, "moveFactor");
	Shader shdrCubemap = LoadShader("resources/shaders/cubemap.vert", "resources/shaders/cubemap.frag");
	int param[1] = { MAP_CUBEMAP };
	SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "environmentMapNight"), param, UNIFORM_INT);
	int param2[1] = { MAP_IRRADIANCE };
	SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "environmentMapDay"), param2, UNIFORM_INT);
	int param3[1] = { 0 };
	SetShaderValue(shdrCubemap, GetShaderLocation(shdrCubemap, "equirectangularMap"), param3, UNIFORM_INT);
	Texture2D texHDR = LoadTexture("resources/milkyWay.hdr"); // Load HDR panorama (sphere) texture
	Texture2D texHDR2 = LoadTexture("resources/daytime.hdr"); // Load HDR panorama (sphere) texture
	// Generate cubemap (texture with 6 quads-cube-mapping) from panorama HDR texture
	// NOTE: New texture is generated rendering to texture, shader computes the sphere->cube coordinates mapping
	skybox.materials[0].maps[0].texture = LoadTexture("resources/skyGradient.png");
	SetTextureFilter(skybox.materials[0].maps[0].texture, FILTER_BILINEAR);
	SetTextureWrap(skybox.materials[0].maps[0].texture, WRAP_CLAMP);
	skybox.materials[0].maps[MAP_CUBEMAP].texture = GenTextureCubemap(shdrCubemap, texHDR, 1024);
	skybox.materials[0].maps[MAP_IRRADIANCE].texture = GenTextureCubemap(shdrCubemap, texHDR2, 1024);
	SetTextureFilter(skybox.materials[0].maps[MAP_CUBEMAP].texture, FILTER_BILINEAR);
	SetTextureFilter(skybox.materials[0].maps[MAP_IRRADIANCE].texture, FILTER_BILINEAR);
	GenTextureMipmaps(&skybox.materials[0].maps[MAP_CUBEMAP].texture);
	GenTextureMipmaps(&skybox.materials[0].maps[MAP_IRRADIANCE].texture);
	UnloadTexture(texHDR);      // Texture not required anymore, cubemap already generated
	UnloadTexture(texHDR2);      // Texture not required anymore, cubemap already generated
	UnloadShader(shdrCubemap);  // Unload cubemap generation shader, not required anymore

	// TREES
	Texture2D treeTextures[TREE_TEXTURE_COUNT];
	for (size_t i = 0; i < TREE_TEXTURE_COUNT; i++)
	{
		treeTextures[i] = LoadTexture(TextFormat("resources/trees/b/%i.png", i)); // variant b of trees looks much better
		SetTextureFilter(treeTextures[i], FILTER_BILINEAR);
		//GenTextureMipmaps(&treeTextures[i]); // looks better without
	}
	GenerateTrees(erosionMaker, mapData, treeTextures, &trees, true);
	Material treeMaterial = LoadMaterialDefault();
	treeShader = LoadShader("resources/shaders/vegetation.vert", "resources/shaders/vegetation.frag");
	treeShader.locs[LOC_MATRIX_MODEL] = GetShaderLocation(treeShader, "matModel");
	int treeAmbientLoc = GetShaderLocation(treeShader, "ambient");
	SetShaderValue(treeShader, treeAmbientLoc, ambc, UNIFORM_VEC4);
	treeMaterial.shader = treeShader;
	treeMaterial.maps[1].texture = DUDVTex;
	float treeMoveFactor = 0.0f;
	int treeMoveFactorLoc = GetShaderLocation(treeShader, "moveFactor");

	// LIGHT(S)
	Light lights[MAX_LIGHTS] = { 0 };
	lights[0] = CreateLight(LIGHT_DIRECTIONAL, { 20, 10, 0 }, Vector3Zero(), WHITE, { terrainModel.materials[0].shader, oceanModel.materials[0].shader, treeShader, skybox.materials[0].shader });

	float angle = 6.282f;
	float radius = 100.0f;

	//rlDisableBackfaceCulling();

	SetTargetFPS(0); // Set our game to run at 60 frames-per-second
	SetTraceLogLevel(LOG_NONE); // disable logging from now on
	//--------------------------------------------------------------------------------------

	// Main game loop
	while (!WindowShouldClose()) // Detect window close button or ESC key
	{
		if (IsWindowResized() || windowSizeChanged)
		{
			windowSizeChanged = false;
			// resize fbos based on screen size
			UnloadRenderTexture(applicationBuffer);
			UnloadRenderTexture(reflectionBuffer);
			UnloadRenderTexture(refractionBuffer);
			applicationBuffer = LoadRenderTexture(GetScreenWidth(), GetScreenHeight()); // main FBO used for postprocessing
			reflectionBuffer = LoadRenderTexture(GetScreenWidth() / fboSize, GetScreenHeight() / fboSize); // FBO used for water reflection
			refractionBuffer = LoadRenderTexture(GetScreenWidth() / fboSize, GetScreenHeight() / fboSize); // FBO used for water refraction
			SetTextureFilter(reflectionBuffer.texture, FILTER_BILINEAR);
			SetTextureFilter(refractionBuffer.texture, FILTER_BILINEAR);

			// to be sure
			oceanModel.materials[0].maps[0].texture = reflectionBuffer.texture; // uniform texture0
			oceanModel.materials[0].maps[1].texture = refractionBuffer.texture; // uniform texture1

			SetTraceLogLevel(LOG_INFO);
			TraceLog(LOG_INFO, TextFormat("Window resized: %d x %d", GetScreenWidth(), GetScreenHeight()));
			SetTraceLogLevel(LOG_NONE);
		}
		// Update
		//----------------------------------------------------------------------------------
		if (!IsKeyDown(KEY_LEFT_ALT))
		{
			if (!IsCursorHidden())
			{
				DisableCursor();
			}
			UpdateCamera(&camera); // Update camera
		}
		else
		{
			EnableCursor();
		}

		// animate water
		waterMoveFactor += 0.03f * GetFrameTime();
		while (waterMoveFactor > 1.0f)
		{
			waterMoveFactor -= 1.0;
		}
		SetShaderValue(oceanModel.materials[0].shader, waterMoveFactorLoc, &waterMoveFactor, UNIFORM_FLOAT);

		// animate trees
		treeMoveFactor += 0.125f * GetFrameTime();
		while (treeMoveFactor > 1.0f)
		{
			treeMoveFactor -= 1.0;
		}
		SetShaderValue(treeShader, treeMoveFactorLoc, &treeMoveFactor, UNIFORM_FLOAT);

		// animate cirrostratus
		cloudMoveFactor += 0.0032f * GetFrameTime();
		while (cloudMoveFactor > 1.0f)
		{
			cloudMoveFactor -= 1.0;
		}
		SetShaderValue(cloudModel.materials[0].shader, cloudMoveFactorLoc, &cloudMoveFactor, UNIFORM_FLOAT);

		// animate daytime clouds
		skyboxMoveFactor += 0.0085f * GetFrameTime();
		while (skyboxMoveFactor > 1.0f)
		{
			skyboxMoveFactor -= 1.0;
		}
		SetShaderValue(skybox.materials[0].shader, skyboxMoveFactorLoc, &skyboxMoveFactor, UNIFORM_FLOAT);

		// animate daytime
		if (dayrunning)
		{
			daytime += dayspeed * GetFrameTime();
			while (daytime > 1.0f)
			{
				daytime -= 1.0;
			}
		}
		if (IsKeyDown(KEY_SPACE))
		{
			daytime += dayspeed * (5.0f - (float)dayrunning) * GetFrameTime();
			while (daytime > 1.0f)
			{
				daytime -= 1.0;
			}
		}
		float sunAngle = Lerp(-90, 270, daytime) * DEG2RAD; // -90 midnight, 90 midday
		float nDaytime = sinf(sunAngle); // normalize it to make it look like a dot product on an unit sphere (shaders expect it this way) (-1, 1)
		int iDaytime = ((nDaytime + 1.0f) / 2.0f) * (float)(ambientColorsNumber - 1);
		ambc[0] = ambientColors[iDaytime].x; // ambient color based on daytime
		ambc[1] = ambientColors[iDaytime].y;
		ambc[2] = ambientColors[iDaytime].z;
		ambc[3] = Lerp(0.05f, 0.25f, ((nDaytime + 1.0f) / 2.0f)); // ambient strength based on daytime
		SetShaderValue(terrainModel.materials[0].shader, terrainDaytimeLoc, &nDaytime, UNIFORM_FLOAT);
		SetShaderValue(skybox.materials[0].shader, skyboxDaytimeLoc, &nDaytime, UNIFORM_FLOAT);
		SetShaderValue(skybox.materials[0].shader, skyboxDayrotationLoc, &daytime, UNIFORM_FLOAT);
		SetShaderValue(cloudModel.materials[0].shader, cloudDaytimeLoc, &nDaytime, UNIFORM_FLOAT);
		SetShaderValue(terrainModel.materials[0].shader, terrainAmbientLoc, ambc, UNIFORM_VEC4);
		SetShaderValue(treeShader, treeAmbientLoc, ambc, UNIFORM_VEC4);

		// Make the light orbit
		lights[0].position.x = cosf(sunAngle) * radius;
		lights[0].position.y = sinf(sunAngle) * radius;
		lights[0].position.z = std::max(sinf(sunAngle) * radius * 0.9f, -radius / 4.0f); // skew sun orbit

		UpdateLightValues(lights[0]);

		// Update the light shader with the camera view position
		float cameraPos[3] = { camera.position.x, camera.position.y, camera.position.z };
		SetShaderValue(terrainModel.materials[0].shader, terrainModel.materials[0].shader.locs[LOC_VECTOR_VIEW], cameraPos, UNIFORM_VEC3);
		SetShaderValue(oceanModel.materials[0].shader, oceanModel.materials[0].shader.locs[LOC_VECTOR_VIEW], cameraPos, UNIFORM_VEC3);
		//----------------------------------------------------------------------------------

		// Draw
		//----------------------------------------------------------------------------------
		BeginDrawing();

		// render stuff to reflection FBO
		BeginTextureMode(reflectionBuffer);
		ClearBackground(RED);
		camera.position.y *= -1;
		Render3DScene(camera, lights, { skybox, terrainModel }, noTrees, 1);
		camera.position.y *= -1;
		EndTextureMode();

		// render stuff to refraction FBO
		BeginTextureMode(refractionBuffer);
		ClearBackground(GREEN);
		Render3DScene(camera, lights, { skybox, terrainModel, oceanFloorModel }, noTrees, 0);
		EndTextureMode();

		// render stuff to normal application buffer
		if (useApplicationBuffer) BeginTextureMode(applicationBuffer);
		ClearBackground(YELLOW);
		Render3DScene(camera, lights, { skybox, cloudModel, terrainModel, oceanFloorModel, oceanModel }, trees, 2);
		if (useApplicationBuffer) EndTextureMode();

		// render to frame buffer after applying post-processing (if enabled)
		if (useApplicationBuffer)
		{
			BeginShaderMode(postProcessShader);
			// NOTE: Render texture must be y-flipped due to default OpenGL coordinates (left-bottom)
			DrawTextureRec(applicationBuffer.texture, { 0.0f, 0.0f, (float)applicationBuffer.texture.width, (float)-applicationBuffer.texture.height }, { 0.0f, 0.0f }, WHITE);
			EndShaderMode();
		}

		int hour = daytime * 24.0f;
		int minute = (daytime * 24.0f - (float)hour) * 60.0f;
		// render GUI
		if (!IsKeyDown(KEY_F6))
		{
			if (!IsKeyDown(KEY_F1))
			{
				DrawText("Hold F1 to display controls. Hold ALT to enable cursor.", 10, 10, 20, WHITE);
				DrawText(TextFormat("Droplets simulated: %i", totalDroplets), 10, 40, 20, WHITE);
				DrawText(TextFormat("FPS: %2i", GetFPS()), 10, 70, 20, WHITE);

				DrawText(TextFormat("%02d : %02d", hour, minute), GetScreenWidth() - 80, 10, 20, WHITE);
			}
			else
			{
				DrawText("Z - hold to erode\nX - press to erode 100000 droplets\nR - press to reset island (chebyshev)\nT - press to reset island (euclidean)\nY - press to reset island (manhattan)\nU - press to reset island (star)\nCTRL - toggle sun movement\nSpace - advance daytime\nS - display frame buffers\nA - display debug\nF2 - toggle 60 FPS lock\nF3 - change window resolution\nF4 - toggle fullscreen\nF5 - toggle application buffer\nF6 - hold to hide GUI\nF9 - take screenshot", 10, 10, 20, WHITE);
			}
		}

		if (IsKeyDown(KEY_Z))
		{
			// Erode
			const int spd = 350;
			erosionMaker->Erode(mapData, MAP_RESOLUTION, spd, false);
			totalDroplets += spd;
			dropletsSinceLastTreeRegen += spd;

			// Update pixels
			for (size_t i = 0; i < MAP_RESOLUTION * MAP_RESOLUTION; i++)
			{
				int val = mapData->at(i) * 255;
				pixels[i].r = val;
				pixels[i].g = val;
				pixels[i].b = val;
				pixels[i].a = 255;
			}
			UnloadTexture(heightmapTexture);
			Image heightmapImage = LoadImageEx(pixels, MAP_RESOLUTION, MAP_RESOLUTION);
			heightmapTexture = LoadTextureFromImage(heightmapImage); // Convert image to texture (VRAM)
			SetTextureFilter(heightmapTexture, FILTER_BILINEAR);
			SetTextureWrap(heightmapTexture, WRAP_CLAMP);
			UnloadImage(heightmapImage); // Unload heightmap image from RAM, already uploaded to VRAM

			if (dropletsSinceLastTreeRegen > spd * 10)
			{
				GenerateTrees(erosionMaker, mapData, treeTextures, &trees, false);
				dropletsSinceLastTreeRegen = 0;
			}
		}
		if (IsKeyPressed(KEY_X))
		{
			// Erode
			std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
			erosionMaker->Erode(mapData, MAP_RESOLUTION, 100000, false);
			std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

			SetTraceLogLevel(LOG_INFO);
			TraceLog(LOG_INFO, TextFormat("Eroded 100000 droplets. Time elapsed: %f s", std::chrono::duration_cast<std::chrono::nanoseconds> (end - begin).count() / 1000000000.0));
			SetTraceLogLevel(LOG_NONE);

			totalDroplets += 100000;
			// Update pixels
			for (size_t i = 0; i < MAP_RESOLUTION * MAP_RESOLUTION; i++)
			{
				int val = mapData->at(i) * 255;
				pixels[i].r = val;
				pixels[i].g = val;
				pixels[i].b = val;
				pixels[i].a = 255;
			}
			UnloadTexture(heightmapTexture);
			Image heightmapImage = LoadImageEx(pixels, MAP_RESOLUTION, MAP_RESOLUTION);
			heightmapTexture = LoadTextureFromImage(heightmapImage); // Convert image to texture (VRAM)
			SetTextureFilter(heightmapTexture, FILTER_BILINEAR);
			SetTextureWrap(heightmapTexture, WRAP_CLAMP);
			UnloadImage(heightmapImage); // Unload heightmap image from RAM, already uploaded to VRAM

			GenerateTrees(erosionMaker, mapData, treeTextures, &trees, false);
			dropletsSinceLastTreeRegen = 0;
		}
		if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_T) || IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_U))
		{
			totalDroplets = 0;
			pixels = GetImageData(initialHeightmapImage);
			for (size_t i = 0; i < MAP_RESOLUTION * MAP_RESOLUTION; i++)
			{
				mapData->at(i) = pixels[i].r / 255.0f;
			}
			// reinit map
			if (IsKeyPressed(KEY_R))
			{
				erosionMaker->Gradient(mapData, MAP_RESOLUTION, 0.5f, GradientType::SQUARE);
			}
			else if (IsKeyPressed(KEY_T)) 
			{
				erosionMaker->Gradient(mapData, MAP_RESOLUTION, 0.5f, GradientType::CIRCLE);
			}
			else if (IsKeyPressed(KEY_Y))
			{
				erosionMaker->Gradient(mapData, MAP_RESOLUTION, 0.5f, GradientType::DIAMOND);
			}
			else if (IsKeyPressed(KEY_U))
			{
				erosionMaker->Gradient(mapData, MAP_RESOLUTION, 0.5f, GradientType::STAR);
			}
			erosionMaker->Remap(mapData, MAP_RESOLUTION); // flatten beaches
			// no need to reinitialize erosion
			// Update pixels
			for (size_t i = 0; i < MAP_RESOLUTION * MAP_RESOLUTION; i++)
			{
				int val = mapData->at(i) * 255;
				pixels[i].r = val;
				pixels[i].g = val;
				pixels[i].b = val;
				pixels[i].a = 255;
			}
			UnloadTexture(heightmapTexture);
			Image heightmapImage = LoadImageEx(pixels, MAP_RESOLUTION, MAP_RESOLUTION);
			heightmapTexture = LoadTextureFromImage(heightmapImage); // Convert image to texture (VRAM)
			SetTextureFilter(heightmapTexture, FILTER_BILINEAR);
			SetTextureWrap(heightmapTexture, WRAP_CLAMP);
			UnloadImage(heightmapImage); // Unload heightmap image from RAM, already uploaded to VRAM

			GenerateTrees(erosionMaker, mapData, treeTextures, &trees, false);
			dropletsSinceLastTreeRegen = 0;
		}

		if (IsKeyDown(KEY_S))
		{
			// display FBOS for debug
			DrawTextureRec(reflectionBuffer.texture, { 0.0f, 0.0f, (float)reflectionBuffer.texture.width, (float)-reflectionBuffer.texture.height }, { 0.0f, 0.0f }, WHITE);
			DrawTextureRec(refractionBuffer.texture, { 0.0f, 0.0f, (float)refractionBuffer.texture.width, (float)-refractionBuffer.texture.height }, { 0.0f, (float)reflectionBuffer.texture.height }, WHITE);
		}
		if (IsKeyDown(KEY_A))
		{
			// display other info for debug
			DrawTextureEx(heightmapTexture, { GetScreenWidth() - heightmapTexture.width - 20.0f, 20 }, 0, 1, WHITE);
			DrawRectangleLines(GetScreenWidth() - heightmapTexture.width - 20, 20, heightmapTexture.width, heightmapTexture.height, GREEN);

			//DrawFPS(10, 70);
		}

		if (IsKeyPressed(KEY_LEFT_CONTROL))
		{
			dayrunning = !dayrunning;
		}

		if (IsKeyPressed(KEY_F2))
		{
			if (lockTo60FPS)
			{
				lockTo60FPS = false;
				SetTargetFPS(0);
			}
			else
			{
				lockTo60FPS = true;
				SetTargetFPS(60);
			}
		}
		if (IsKeyPressed(KEY_F3))
		{
			currentDisplayResolutionIndex++;
			if (currentDisplayResolutionIndex > 4)
				currentDisplayResolutionIndex = 0;

			windowSizeChanged = true;
			SetWindowSize(displayResolutions[currentDisplayResolutionIndex].x, displayResolutions[currentDisplayResolutionIndex].y);
			SetWindowPosition((GetMonitorWidth(0) - GetScreenWidth()) / 2, (GetMonitorHeight(0) - GetScreenHeight()) / 2);
		}
		if (IsKeyPressed(KEY_F4))
		{
			windowSizeChanged = true;
			if (!IsWindowFullscreen())
			{
				windowWidthBeforeFullscreen = GetScreenWidth();
				windowHeightBeforeFullscreen = GetScreenHeight();
				SetWindowSize(GetMonitorWidth(0), GetMonitorHeight(0));
			}
			else
			{
				SetWindowSize(windowWidthBeforeFullscreen, windowHeightBeforeFullscreen);
			}
			ToggleFullscreen();
		}

		if (IsKeyPressed(KEY_F5))
		{
			useApplicationBuffer = !useApplicationBuffer;
		}

		if (IsKeyPressed(KEY_F9))
		{
			// take a screenshot
			for (int i = 0; i < INT_MAX; i++)
			{
				const char* fileName = TextFormat("screen%i.png", i);
				if (FileExists(fileName) == 0)
				{
					TakeScreenshot(fileName);
					break;
				}
			}
		}
		EndDrawing();
		//----------------------------------------------------------------------------------
	}

	// De-Initialization
	//--------------------------------------------------------------------------------------

	// technically not required
	UnloadRenderTexture(applicationBuffer);
	UnloadRenderTexture(reflectionBuffer);
	UnloadRenderTexture(refractionBuffer);

	CloseWindow(); // Close window and OpenGL context
	//--------------------------------------------------------------------------------------

	return 0;
}

void Render3DScene(Camera camera, Light lights[], std::vector<Model> models, std::vector<TreeBillboard> trees, int clipPlane)
{
	BeginMode3D(camera);
	for (size_t i = 0; i < CLIP_SHADERS_COUNT; i++) // setup clip plane for shaders that use it
	{
		SetShaderValue(clipShaders[i], clipShaderTypeLocs[i], &clipPlane, UNIFORM_INT);
	}

	for (size_t i = 0; i < models.size(); i++) // draw all 3d models in the scene
	{
		DrawModel(models[i], { 0, 0, 0 }, 1.0f, WHITE);
	}

	BeginShaderMode(treeShader);
	for (size_t i = 0; i < trees.size(); i++) // draw all trees
	{
		DrawBillboard(camera, trees[i].texture, trees[i].position, trees[i].scale, trees[i].color);
	}
	EndShaderMode();

	// Draw markers to show where the lights are
	/*for (size_t i = 0; i < MAX_LIGHTS; i++)
	{
		if (lights[i].enabled) { DrawSphereEx(Vector3Scale(lights[0].position, 50), 100, 8, 8, RED); }
	}*/
	EndMode3D();
}

void GenerateTrees(ErosionMaker* erosionMaker, std::vector<float>* mapData, Texture2D* treeTextures, std::vector<TreeBillboard>* trees, bool generateNew)
{
	Vector3 billPosition = { 0.0f, 0.0f, 0.0f };
	Vector3 billNormal = { 0.0f, 0.0f, 0.0f };
	float grassSlopeThreshold = 0.2; // different than in the terrain shader
	float grassBlendAmount = 0.55;
	float grassWeight;
	Color billColor = WHITE;

	for (size_t i = 0; i < TREE_COUNT; i++) // 8190 max billboards, more than that and they are not cached anymore
	{
		int px, py;
		do
		{
			// try to generate a billboard
			billPosition.x = randomRange(-16, 16);
			billPosition.z = randomRange(-16, 16);
			px = ((billPosition.x + 16.0f) / 32.0f) * (MAP_RESOLUTION - 1);
			py = ((billPosition.z + 16.0f) / 32.0f) * (MAP_RESOLUTION - 1);
			billNormal = erosionMaker->GetNormal(mapData, MAP_RESOLUTION, px, py);
			billPosition.y = mapData->at(py * MAP_RESOLUTION + px) * 8 - 1.1f;

			float slope = 1.0 - billNormal.y;
			float grassBlendHeight = grassSlopeThreshold * (1.0 - grassBlendAmount);
			grassWeight = 1.0 - std::min(std::max((slope - grassBlendHeight) / (grassSlopeThreshold - grassBlendHeight), 0.0f), 1.0f);
		} while (billPosition.y < 0.32f || billPosition.y > 3.25f || grassWeight < 0.65f); // repeat until you find valid parameters (height and normal of chosen spot)

		billColor.r = (billNormal.x + 1) * 127.5f; // terrain normal where tree is located, stored on color
		billColor.g = (billNormal.y + 1) * 127.5f; // convert from range (-1, 1) to (0, 255)
		billColor.b = (billNormal.z + 1) * 127.5f;

		if (!generateNew)
		{
			(*trees)[i].position = billPosition;
			(*trees)[i].color = billColor;
		}
		else
		{
			int textureChoice = (int)randomRange(0, TREE_TEXTURE_COUNT);
			trees->push_back({ treeTextures[textureChoice], billPosition, randomRange(0.6f, 1.4f) * 0.3f, billColor });
		}
	}
}