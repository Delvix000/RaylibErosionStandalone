#ifndef EROSION_MAKER
#define EROSION_MAKER

#include <iostream>
#include <vector>
#include "raylib.h"

// used to sample a point in the heightmap and get the gradient
typedef struct
{
	float height;
	float gradientX;
	float gradientY;
} HeightAndGradient;

// describes the shape of the smoothing to apply to map borders
enum GradientType
{
	SQUARE = 0,
	CIRCLE = 1,
	DIAMOND = 2,
	STAR = 3,
};

// singleton responsible for simulating erosion on a heightmap
class ErosionMaker
{
public:
	static ErosionMaker& GetInstance()
	{
		static ErosionMaker instance; // Guaranteed to be destroyed.
									  // Instantiated on first use.
		return instance;
	}

private:
	ErosionMaker() {} // Constructor? (the {} brackets) are needed here.

	// C++ 03
	// ========
	// Don't forget to declare these two. You want to make sure they
	// are inaccessible(especially from outside), otherwise, you may accidentally get copies of
	// your singleton appearing.
	ErosionMaker(ErosionMaker const&);   // Don't Implement
	void operator=(ErosionMaker const&); // Don't Implement

	// C++ 11
	// =======
	// We can use the better technique of deleting the methods
	// we don't want.

	// indices and weights of erosion brush precomputed for every cell
	// it's a cache used to speed up the area of effect erosion of a droplet
	std::vector<std::vector<int>*>* erosionBrushIndices = nullptr; // for each cell, a reference to neighbors is held
	std::vector<std::vector<float>*>* erosionBrushWeights = nullptr; // for each cell, a reference to how much it influences neighbors

	int currentSeed; // current random seed
	int currentErosionRadius; 
	int currentMapSize;

	void Initialize(int mapSize, bool resetSeed); 
	HeightAndGradient CalculateHeightAndGradient(std::vector<float>* nodes, int mapSize, float posX, float posY); // calculates height and gradient of a spot in the map
	void InitializeBrushIndices(int mapSize, int radius); // initialize the brush cache
	float RemapValue(float value); // remaps a single value of a map to nonlinear scale in order to smooth beach areas

public:
	//ErosionMaker(ErosionMaker const &) = delete;
	//void operator=(ErosionMaker const &) = delete;

	// Note: Scott Meyers mentions in his Effective Modern
	//       C++ book, that deleted functions should generally
	//       be public as it results in better error messages
	//       due to the compilers behavior to check accessibility
	//       before deleted status

	//int seed = 0;
	int erosionRadius = 6;//12; // Range (2, 8)

	float inertia = 0.05f; // range (0, 1) at zero, water will instantly change direction to flow downhill. At 1, water will never change direction. 
	float sedimentCapacityFactor = 6.0f; // multiplier for how much sediment a droplet can carry
	float minSedimentCapacity = 0.01f; // used to prevent carry capacity getting too close to zero on flatter terrain
	float erodeSpeed = 0.3f; // range (0, 1) how easily a droplet removes sediment
	float depositSpeed = 0.3f; // range (0, 1) how easily a droplet deposits sediment
	float evaporateSpeed = 0.01f; // range (0, 1) droplets evaporate during their lifetime, reducing mass
	float gravity = 4.0f; // determines speed increase of the droplet upon a slope
	int maxDropletLifetime = 60;

	float initialWaterVolume = 1;
	float initialSpeed = 1;

	void Erode(std::vector<float>* map, int mapSize, int numIterations = 1, bool resetSeed = false); // applies erosion to the map
	void Gradient(std::vector<float>* map, int mapSize, float normalizedOffset, GradientType gradientType); // allpies a gradient to the map in order to get flat borders
	Vector3 GetNormal(std::vector<float>* map, int mapSize, int x, int y); // gets the normal of a point in the map using interpolation
	void Remap(std::vector<float>* map, int mapSize); // applies a filter to the map in order to flatten beach areas by remapping normalized values
};

#endif