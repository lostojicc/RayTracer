#include "GpuUpload.h"

#include "Core/Renderer/Triangle.h"

namespace GpuUpload {

	std::vector<GPUTriangle> BuildGPUTriangles(const Core::Mesh& mesh)
	{
		std::vector<GPUTriangle> result;
		result.reserve(mesh.GetTriangleCount());

		for (uint32_t triangleId = 0; triangleId < mesh.GetTriangleCount(); triangleId++)
		{
			const Core::Triangle triangle = mesh.GetTriangle(triangleId);

			GPUTriangle gpuTriangle;
			gpuTriangle.V0 = triangle.V0;
			gpuTriangle.V1 = triangle.V1;
			gpuTriangle.V2 = triangle.V2;
			result.push_back(gpuTriangle);
		}

		return result;
	}

	std::vector<GPUBVHNode> BuildGPUBVHNodes(const Core::BVH& bvh)
	{
		std::vector<GPUBVHNode> result;
		result.reserve(bvh.GetNodes().size());

		for (const Core::BVHNode& node : bvh.GetNodes())
		{
			GPUBVHNode gpuNode;
			gpuNode.BoundsMin = node.Bounds.Min;
			gpuNode.BoundsMax = node.Bounds.Max;
			gpuNode.FirstPrimitive = node.FirstPrimitive;
			gpuNode.PrimitiveCount = node.PrimitiveCount;
			gpuNode.LeftChild = node.LeftChild;
			gpuNode.RightChild = node.RightChild;
			result.push_back(gpuNode);
		}

		return result;
	}

	std::vector<uint32_t> BuildGPUBVHPrimitiveIds(const Core::BVH& bvh)
	{
		std::vector<uint32_t> result;
		result.reserve(bvh.GetPrimitives().size());

		for (const Core::BVHPrimitive& primitive : bvh.GetPrimitives())
			result.push_back(primitive.TriangleId);

		return result;
	}

}
