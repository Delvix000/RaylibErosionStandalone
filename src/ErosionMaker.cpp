#include "ErosionMaker.h"
#include <math.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <cstdlib> 
#include <ctime> 
#include "raymath.h"
#include "raylib.h"

void ErosionMaker::Initialize(int mapSize, bool resetSeed)
{
	// initialization randomizes the generator and precomputes indices and weights of erosion brush

	if (resetSeed)
	{
		unsigned int newseed = (unsigned)time(0);
		srand(newseed);
		currentSeed = newseed;
	}

	if (erosionBrushIndices == nullptr || currentErosionRadius != erosionRadius || currentMapSize != mapSize)
	{
		InitializeBrushIndices(mapSize, erosionRadius);
		currentErosionRadius = erosionRadius;
		currentMapSize = mapSize;
	}
}

// simulate erosion with the given amount of droplets
void ErosionMaker::Erode(std::vector<float>* mapData, int mapSize, int dropletAmount, bool resetSeed)
{
	Initialize(mapSize, resetSeed);

	for (int iteration = 0; iteration < dropletAmount; iteration++)
	{
		// create water droplet at random point on map (not bound to cell)
		float posX = (rand() % (mapSize - 1)) + 0;
		float posY = (rand() % (mapSize - 1)) + 0;
		float dirX = 0;
		float dirY = 0;
		float speed = initialSpeed;
		float water = initialWaterVolume;
		float sediment = 0; // sediment currently carried

		for (int lifetime = 0; lifetime < maxDropletLifetime; lifetime++)
		{
			// droplet position bound to cell
			int nodeX = (int)posX;
			int nodeY = (int)posY;
			int dropletIndex = nodeY * mapSize + nodeX;
			// calculate droplet's offset inside the cell (0,0) = at NW node, (1,1) = at SE node
			float cellOffsetX = posX - (float)nodeX;
			float cellOffsetY = posY - (float)nodeY;

			// calculate droplet's height and direction of flow with bilinear interpolation of surrounding heights
			HeightAndGradient heightAndGradient = CalculateHeightAndGradient(mapData, mapSize, posX, posY);

			// update the droplet's direction and position (move position 1 unit regardless of speed)
			dirX = (dirX * inertia - heightAndGradient.gradientX * (1 - inertia)); // lerp with old dir by using inertia as mix value
			dirY = (dirY * inertia - heightAndGradient.gradientY * (1 - inertia));

			// normalize direction
			float len = sqrtf(dirX * dirX + dirY * dirY);
			if (len > 0.0001f)
			{
				dirX /= len;
				dirY /= len;
			}
			// update droplet position based on direction (move 1 unit)
			posX += dirX;
			posY += dirY;

			// stop simulating droplet if it's not moving or has flowed over edge of map
			if ((dirX == 0 && dirY == 0) || posX < 0 || posX >= mapSize - 1 || posY < 0 || posY >= mapSize - 1)
			{
				break;
			}

			// find the droplet's new height and calculate the deltaHeight
			float newHeight = CalculateHeightAndGradient(mapData, mapSize, posX, posY).height;
			float deltaHeight = newHeight - heightAndGradient.height;

			// calculate the droplet's sediment capacity (higher when moving fast down a slope and contains lots of water)
			float sedimentCapacity = std::max(-deltaHeight * speed * water * sedimentCapacityFactor, minSedimentCapacity);

			// if carrying more sediment than capacity, or if flowing uphill:
			if (sediment > sedimentCapacity || deltaHeight > 0)
			{
				// DEPOSIT

				// if moving uphill (deltaHeight > 0) try fill up to the current height, otherwise deposit a fraction of the excess sediment
				float amountToDeposit = (deltaHeight > 0) ? std::min(deltaHeight, sediment) : (sediment - sedimentCapacity) * depositSpeed;
				sediment -= amountToDeposit;

				// add the sediment to the four nodes of the current cell using bilinear interpolation
				// deposition is not distributed over a radius (like erosion) so that it can fill small pits
				(*mapData)[(size_t)dropletIndex] += amountToDeposit * (1 - cellOffsetX) * (1 - cellOffsetY);
				(*mapData)[(size_t)dropletIndex + 1] += amountToDeposit * cellOffsetX * (1 - cellOffsetY);
				(*mapData)[(size_t)dropletIndex + mapSize] += amountToDeposit * (1 - cellOffsetX) * cellOffsetY;
				(*mapData)[(size_t)dropletIndex + mapSize + 1] += amountToDeposit * cellOffsetX * cellOffsetY;
			}
			else
			{
				// ERODE

				// erode a fraction of the droplet's current carry capacity.
				// clamp the erosion to the change in height so that it doesn't dig a hole in the terrain behind the droplet
				float amountToErode = std::min((sedimentCapacity - sediment) * erodeSpeed, -deltaHeight);

				// use erosion brush to erode from all nodes inside the droplet's erosion radius
				for (int brushPointIndex = 0; brushPointIndex < (*erosionBrushIndices)[dropletIndex]->size(); brushPointIndex++)
				{
					int nodeIndex = (*(*erosionBrushIndices)[dropletIndex])[brushPointIndex];
					float weighedErodeAmount = amountToErode * (*(*erosionBrushWeights)[dropletIndex])[brushPointIndex];
					float deltaSediment = ((*mapData)[nodeIndex] < weighedErodeAmount) ? (*mapData)[nodeIndex] : weighedErodeAmount;
					(*mapData)[nodeIndex] -= deltaSediment;
					sediment += deltaSediment;
				}
			}

			// update droplet's speed and water content
			speed = sqrtf(speed * speed + deltaHeight * gravity);
			if (isnan(speed))
				speed = 0; // fix per alcuni NaN dovuti a speed * speed + deltaHeight * gravity negativo
			water *= (1 - evaporateSpeed); // evaporate water
		}
	}
}

// applies a radial gradient to the heightmap in order to flatten the outer borders
void ErosionMaker::Gradient(std::vector<float>* mapData, int mapSize, float normalizedOffset, GradientType gradientType)
{
	float radius = ((float)mapSize / 2.0f);
	for (size_t y = 0; y < mapSize; y++)
	{
		for (size_t x = 0; x < mapSize; x++)
		{
			int index = y * mapSize + x;
			float gradient = 0.0f;
			switch (gradientType) {
			case GradientType::SQUARE:
				gradient = std::max(std::abs((float)x - radius), std::abs((float)y - radius)) / (radius); // Chebyshev distance
				break;

			case GradientType::CIRCLE:
				gradient = std::min(((x - radius) * (x - radius) + (y - radius) * (y - radius)) / (radius * radius), 1.0f); // Euclidean distance
				break;

			case GradientType::DIAMOND:
				gradient = std::min((std::abs((float)x - radius) + std::abs((float)y - radius)) / (radius), 1.0f); // Manhattan distance
				break;

			case GradientType::STAR:
			{
				float g1 = std::min((std::abs((float)x - radius) + std::abs((float)y - radius)) / (radius), 1.0f); // Manhattan distance
				float g2 = std::max(std::abs((float)x - radius), std::abs((float)y - radius)) / (radius); // Chebyshev distance
				gradient = Lerp(g1, g2, 0.7f); // mix manhattan and chebyshev by desired value
			}
				break;

			default:
				gradient = std::min(((x - radius) * (x - radius) + (y - radius) * (y - radius)) / (radius * radius), 1.0f); // Euclidean distance
				break;
			}
			gradient = 1 - gradient; // invert
			(*mapData)[index] *= gradient; // multiply height by given linear gradient
		}
	}
}

HeightAndGradient ErosionMaker::CalculateHeightAndGradient(std::vector<float>* mapData, int mapSize, float posX, float posY)
{
	int coordX = (int)posX;
	int coordY = (int)posY;

	// calculate droplet's offset inside the cell (0,0) = at NW node, (1,1) = at SE node
	float x = posX - (float)coordX;
	float y = posY - (float)coordY;

	// calculate heights of the four nodes of the droplet's cell
	int nodeIndexNW = coordY * mapSize + coordX;

	float heightNW = (*mapData)[(size_t)nodeIndexNW];
	float heightNE = (*mapData)[(size_t)nodeIndexNW + 1];
	float heightSW = (*mapData)[(size_t)nodeIndexNW + mapSize];
	float heightSE = (*mapData)[(size_t)nodeIndexNW + mapSize + 1];

	// calculate droplet's direction of flow with bilinear interpolation of height difference along the edges
	float gradientX = (heightNE - heightNW) * (1 - y) + (heightSE - heightSW) * y;
	float gradientY = (heightSW - heightNW) * (1 - x) + (heightSE - heightNE) * x;

	// calculate height with bilinear interpolation of the heights of the nodes of the cell
	float height = heightNW * (1 - x) * (1 - y) + heightNE * x * (1 - y) + heightSW * (1 - x) * y + heightSE * x * y;

	HeightAndGradient ret;
	ret.height = height;
	ret.gradientX = gradientX;
	ret.gradientY = gradientY;
	return ret;
}

void ErosionMaker::InitializeBrushIndices(int mapSize, int radius)
{
	erosionBrushIndices = new std::vector<std::vector<int>*>((size_t)mapSize * mapSize); // each cell stores its neighbors' indices
	erosionBrushWeights = new std::vector<std::vector<float>*>((size_t)mapSize * mapSize); // each cell stores its neighbors' weight

	std::vector<int> xOffsets((size_t)radius * radius * 4, 0);
	std::vector<int> yOffsets((size_t)radius * radius * 4, 0);
	std::vector<float> weights((size_t)radius * radius * 4, 0);
	float weightSum = 0;
	int addIndex = 0;

	for (int i = 0; i < erosionBrushIndices->size(); i++) // va bene la prima coord
	{
		int centreX = i % mapSize; // x coordinate by cell
		int centreY = i / mapSize; // y coodinate by cell

		if (centreY <= radius || centreY >= mapSize - radius || centreX <= radius || centreX >= mapSize - radius) // loop only not too close to borders
		{
			weightSum = 0;
			addIndex = 0;
			for (int y = -radius; y <= radius; y++) // loop neighbors
			{
				for (int x = -radius; x <= radius; x++)
				{
					float sqrDst = x * x + y * y;
					if (sqrDst < radius * radius) // take only those inside the radius of influence
					{
						int coordX = centreX + x;
						int coordY = centreY + y;

						if (coordX >= 0 && coordX < mapSize && coordY >= 0 && coordY < mapSize)
						{
							// add to brush index
							float weight = 1 - sqrtf(sqrDst) / radius; // euclidean distance -> circle
							weightSum += weight;
							weights[addIndex] = weight;
							xOffsets[addIndex] = x;
							yOffsets[addIndex] = y;
							addIndex++;
						}
					}
				}
			}
		}

		int numEntries = addIndex;
		(*erosionBrushIndices)[i] = new std::vector<int>(numEntries);
		(*erosionBrushWeights)[i] = new std::vector<float>(numEntries);

		for (int j = 0; j < numEntries; j++)
		{
			(*(*erosionBrushIndices)[i])[j] = (yOffsets[j] + centreY) * mapSize + xOffsets[j] + centreX;
			(*(*erosionBrushWeights)[i])[j] = weights[j] / weightSum;
		}
	}
}

float ErosionMaker::RemapValue(float value)
{
	const int points = 4;
	// describe the remapping of the grayscale heights (beach and craters)
	const Vector2 point[points] =
	{
		{0.0f,		0.0f}, // initial point (keep)

		{0.15f,		0.16f}, // flatten beach
		{0.2f,		0.16f},
		/*{0.3f,		0.4f}, // add some craters
		{0.4f,		0.3f},*/

		{1.0f,		1.0f}, // final point (keep)
	};

	if (value < 0.0f)
		return value;
	for (size_t i = 1; i < points; i++)
	{
		if (value < point[i].x)
			return Lerp(point[i - 1].y, point[i].y, (value - point[i - 1].x) / (point[i].x - point[i - 1].x)); // lerp based on the interval you're on
	}
	return value;
}

Vector3 ErosionMaker::GetNormal(std::vector<float>* mapData, int mapSize, int x, int y)
{
	// value from trial & error.
	// seems to work fine for the scales we are dealing with.
	// almost equivalent code in terrain shader to get normal
	float strength = 20.0;

	int u, v;

	u = std::min(std::max(x - 1, 0), mapSize - 1);
	v = std::min(std::max(y + 1, 0), mapSize - 1);
	float bl = mapData->at(v * mapSize + u);

	u = std::min(std::max(x, 0), mapSize - 1);
	v = std::min(std::max(y + 1, 0), mapSize - 1);
	float b = mapData->at(v * mapSize + u);

	u = std::min(std::max(x + 1, 0), mapSize - 1);
	v = std::min(std::max(y + 1, 0), mapSize - 1);
	float br = mapData->at(v * mapSize + u);

	u = std::min(std::max(x - 1, 0), mapSize - 1);
	v = std::min(std::max(y, 0), mapSize - 1);
	float l = mapData->at(v * mapSize + u);

	u = std::min(std::max(x + 1, 0), mapSize - 1);
	v = std::min(std::max(y, 0), mapSize);
	float r = mapData->at(v * mapSize + u);

	u = std::min(std::max(x - 1, 0), mapSize - 1);
	v = std::min(std::max(y - 1, 0), mapSize - 1);
	float tl = mapData->at(v * mapSize + u);

	u = std::min(std::max(x, 0), mapSize - 1);
	v = std::min(std::max(y - 1, 0), mapSize - 1);
	float t = mapData->at(v * mapSize + u);

	u = std::min(std::max(x + 1, 0), mapSize - 1);
	v = std::min(std::max(y - 1, 0), mapSize - 1);
	float tr = mapData->at(v * mapSize + u);

	// compute dx using Sobel:
	//           -1 0 1 
	//           -2 0 2
	//           -1 0 1
	float dX = tr + 2.0f * r + br - tl - 2.0f * l - bl;

	// compute dy using Sobel:
	//           -1 -2 -1 
	//            0  0  0
	//            1  2  1
	float dY = bl + 2.0f * b + br - tl - 2.0f * t - tr;

	return Vector3Normalize({ -dX, 1.0f / strength, -dY });
}

void ErosionMaker::Remap(std::vector<float>* map, int mapSize)
{

	for (size_t i = 0; i < mapSize * mapSize; i++)
	{
		map->at(i) = RemapValue(map->at(i));
	}
}
