#include "primitive_extraction.h"
#include "primitive_helper.h"
#include "csgnode.h"
#include "csgnode_helper.h"
#include "helper.h"

lmu::Primitive lmu::createSpherePrimitive(const lmu::ManifoldPtr& m);


std::tuple<lmu::PrimitiveSet, lmu::ManifoldSet> extractStaticManifolds(const lmu::ManifoldSet& manifolds)
{
	lmu::PrimitiveSet primitives;
	lmu::ManifoldSet restManifolds;

	for (const auto& manifold : manifolds)
	{
		if (manifold->type == lmu::ManifoldType::Sphere)
		{
			primitives.push_back(lmu::createSpherePrimitive(manifold));
		}
		else if (manifold->type == lmu::ManifoldType::Cylinder) {
			lmu::ManifoldSet planes;
			primitives.push_back(lmu::createCylinderPrimitive(manifold, planes));
		}
		else
		{
			restManifolds.push_back(manifold);
		}

		// All manifolds are non-static.
		//restManifolds.push_back(manifold);

	}

	return std::make_tuple(primitives, restManifolds);
}

#include <CGAL/Cartesian.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/point_generators_2.h>
#include <CGAL/random_convex_set_2.h>
#include <CGAL/min_quadrilateral_2.h>
#include <CGAL/convex_hull_2.h>
#include <CGAL/Plane_3.h>
#include <CGAL/convex_hull_2.h>

struct Kernel : public CGAL::Cartesian<double> {};

typedef Kernel::Point_2                           Point_2;
typedef Kernel::Line_2                            Line_2;
typedef Kernel::Plane_3                           Plane_3;
typedef Kernel::Point_3                           Point_3;
typedef Kernel::Vector_3                          Vector_3;

typedef CGAL::Polygon_2<Kernel>                   Polygon_2;
typedef CGAL::Random_points_in_square_2<Point_2>  Generator;

std::vector<Point_2> get2DPoints(const lmu::ManifoldPtr& plane, const Eigen::Vector3d* input_points, size_t num_points)
{
	Plane_3 cPlane(Point_3(plane->p.x(), plane->p.y(), plane->p.z()), Vector_3(plane->n.x(), plane->n.y(), plane->n.z()));

	//std::cout << "DEBUG: " << cPlane.to_2d(Point_3(plane->p.x(), plane->p.y(), plane->p.z()));

	std::vector<Point_2> points;
	points.reserve(num_points);
	for (int i = 0; i < num_points; ++i)
	{
		Eigen::Vector3d p = input_points[i];
		points.push_back(cPlane.to_2d(Point_3(p.x(), p.y(), p.z())));
	}

	return points;
}

std::vector<Point_2> get2DPoints(const lmu::ManifoldPtr& plane)
{
	Plane_3 cPlane(Point_3(plane->p.x(), plane->p.y(), plane->p.z()), Vector_3(plane->n.x(), plane->n.y(), plane->n.z()));

	std::vector<Point_2> points;
	points.reserve(plane->pc.rows());
	for (int i = 0; i < plane->pc.rows(); ++i)
	{
		Eigen::Vector3d p = plane->pc.row(i).leftCols(3).transpose();
		points.push_back(cPlane.to_2d(Point_3(p.x(), p.y(), p.z())));
	}

	return points;
}

std::vector<Eigen::Vector3d> get3DPoints(const lmu::ManifoldPtr& plane, const std::vector<Point_2>& points)
{
	Plane_3 cPlane(Point_3(plane->p.x(), plane->p.y(), plane->p.z()), Vector_3(plane->n.x(), plane->n.y(), plane->n.z()));

	std::vector<Eigen::Vector3d> res;
	res.reserve(points.size());
	for (int i = 0; i < points.size(); ++i)
	{
		Point_3 p = cPlane.to_3d(points[i]);
		res.push_back(Eigen::Vector3d(p.x(), p.y(), p.z()));
	}

	return res;
}

lmu::ManifoldSet generateGhostPlanesForSinglePlane(const lmu::ManifoldPtr& plane)
{
	//Project points on plane.
	std::vector<Point_2> points = get2DPoints(plane);

	// One of two algorithms is used, depending on the type of iterator used to specify the input points. 
	// For input iterators, the algorithm used is that of Bykat [Byk78], which has a worst-case running time 
	// of O(n h), where n is the number of input points and h is the number of extreme points. 
	// For all other types of iterators, the O(n logn) algorithm of of Akl and Toussaint [AT78] is used.
	std::vector<Point_2> convHull;
	CGAL::convex_hull_2(points.begin(), points.end(), std::back_inserter(convHull));

	// We use a rotating caliper algorithm [Tou83] with worst case running time linear in the number of input points.
	std::vector<Point_2> rectangle;
	CGAL::min_rectangle_2(convHull.begin(), convHull.end(), std::back_inserter(rectangle));

	if (rectangle.size() != 4)
	{
		std::cout << "Could not create rectangle for plane." << std::endl;
		return lmu::ManifoldSet();
	}

	auto recPts = get3DPoints(plane, rectangle);
	Eigen::Vector3d planeN[4], planeP[4];
	planeN[0] = (recPts[0] - recPts[1]).cross(plane->n).normalized();
	planeN[1] = (recPts[1] - recPts[2]).cross(plane->n).normalized();
	planeN[2] = (recPts[2] - recPts[3]).cross(plane->n).normalized();
	planeN[3] = (recPts[3] - recPts[0]).cross(plane->n).normalized();
	planeP[0] = recPts[0] - 0.5 * (recPts[0] - recPts[1]);
	planeP[1] = recPts[1] - 0.5 * (recPts[1] - recPts[2]);
	planeP[2] = recPts[2] - 0.5 * (recPts[2] - recPts[3]);
	planeP[3] = recPts[3] - 0.5 * (recPts[3] - recPts[0]);

	lmu::ManifoldSet res;
	res.reserve(4);
	for (int i = 0; i < 4; ++i)
	{
		res.push_back(std::make_shared<lmu::Manifold>(
			lmu::ManifoldType::Plane, planeP[i], planeN[i], Eigen::Vector3d(), lmu::PointCloud()));

		//std::cout << "Added ghost plane: " << std::endl << planeP[i] << std::endl << planeN[i] << std::endl;
	}

	return res;
}

lmu::ManifoldSet filterClosePlanes(const lmu::ManifoldSet& ms, double distanceThreshold, double angleThreshold)
{
	lmu::ManifoldSet res;

	for (const auto& plane : ms)
	{
		if (plane->type != lmu::ManifoldType::Plane)
		{
			res.push_back(plane);
			continue;
		}

		bool addPlane = true;
		for (const auto& existingPlane : res)
		{
			if (std::abs((plane->p - existingPlane->p).dot(existingPlane->n.normalized())) < distanceThreshold &&
				std::acos(plane->n.normalized().dot(existingPlane->n.normalized())) < angleThreshold)
			{
				addPlane = false;
				break;
			}
			//else
			//	std::cout << "DT: " <<
			//	std::abs((plane->p - existingPlane->p).dot(existingPlane->n.normalized())) << std::endl;
		}

		if (addPlane)
			res.push_back(plane);
		else
		{
			std::cout << "Removed plane. " << std::endl;
		}
	}

	//std::cout << "MANIFOLDS: " << std::endl;
	//for (const auto& m : ms)
	//	std::cout << *m << std::endl;

	return res;
}

lmu::ManifoldSet lmu::generateGhostPlanes(const PointCloud& pc, const lmu::ManifoldSet& ms, double distanceThreshold,
	double angleThreshold)
{
	lmu::ManifoldSet res = ms;

	for (const auto& m : ms)
	{
		if (m->type == lmu::ManifoldType::Plane)
		{
			auto ghostPlanes = generateGhostPlanesForSinglePlane(m);
			res.insert(res.end(), ghostPlanes.begin(), ghostPlanes.end());
		}
	}

	return filterClosePlanes(res, distanceThreshold * lmu::computeAABBLength(pc), angleThreshold);
}

lmu::GAResult lmu::extractPrimitivesWithGA(const RansacResult& ransacRes)
{
	// Initialize polytope creator.
	initializePolytopeCreator();

	// static primitives are not changed in the GA process but used.
	auto staticPrimsAndRestManifolds = extractStaticManifolds(ransacRes.manifolds);
	auto manifoldsForCreator = std::get<1>(staticPrimsAndRestManifolds);
	auto staticPrimitives = std::get<0>(staticPrimsAndRestManifolds);

	// get union of all non-static manifold pointclouds.
	std::vector<PointCloud> pointClouds; 
	std::transform(manifoldsForCreator.begin(), manifoldsForCreator.end(), std::back_inserter(pointClouds), 
		[](const ManifoldPtr m){return m->pc; });
	auto non_static_pointcloud = lmu::mergePointClouds(pointClouds);

	// Add "ghost planes". 
	double distT = 0.02;
	double angleT = /*M_PI / 18.0*/ M_PI / 9.0;
	//manifoldsForCreator = generateGhostPlanes(ransacRes.pc, manifoldsForCreator, distT, angleT);

	GAResult result;
	PrimitiveSetTournamentSelector selector(2);
	PrimitiveSetIterationStopCriterion criterion(100, 0.00001, 100);

	int maxPrimitiveSetSize = 50;

	PrimitiveSetCreator creator(manifoldsForCreator, 0.0, { 0.4, 0.15, 0.15, 0.15, 0.15 }, 1, 1, maxPrimitiveSetSize, angleT, 0.001);
	//PrimitiveSetCreator creator(manifoldsForCreator, 0.0, { 0.4, 0.15, 0.3, 0.0, 0.15 }, 1, 1, maxPrimitiveSetSize, angleT);
	PrimitiveSetRanker ranker(non_static_pointcloud, ransacRes.manifolds, staticPrimitives, 0.2, maxPrimitiveSetSize);

	//lmu::PrimitiveSetGA::Parameters params(150, 2, 0.7, 0.7, true, Schedule(), Schedule(), false);
	lmu::PrimitiveSetGA::Parameters params(50, 2, 0.4, 0.4, false, Schedule(), Schedule(), false);
	PrimitiveSetGA ga;

	auto res = ga.run(params, selector, creator, ranker, criterion);

	// Try best cutout combination for each satic primitive.
	for (auto& p : staticPrimitives)
	{
		PrimitiveSet ps;
		ps.push_back(p);

		ps[0].cutout = false;
		double score = ranker.rank(ps, true);
		
		ps[0].cutout = true;
		double score_cutout = ranker.rank(ps, true);

		if (score < score_cutout)
			p.cutout = true;
		else
			p.cutout = false;
	}

	result.primitives = ranker.bestPrimitiveSet();//res.population[0].creature;
	result.primitives.insert(result.primitives.end(), staticPrimitives.begin(), staticPrimitives.end());
	result.manifolds = ransacRes.manifolds;

	std::cout << "BEST RANK: " << ranker.rank(ranker.bestPrimitiveSet()) << std::endl;
	std::cout << "-------------------------------------------------" << std::endl;
	std::cout << "Manifold Set: " << std::endl;
	for (const auto& m : ransacRes.manifolds)
		std::cout << *m << std::endl;

	return result;
}

// ==================== CREATOR ====================

lmu::PrimitiveSetCreator::PrimitiveSetCreator(const ManifoldSet& ms, double intraCrossProb,
	const std::vector<double>& mutationDistribution, int maxMutationIterations, int maxCrossoverIterations,
	int maxPrimitiveSetSize, double angleEpsilon,double minDistanceBetweenParallelPlanes) :
	ms(ms),
	intraCrossProb(intraCrossProb),
	mutationDistribution(mutationDistribution),
	maxMutationIterations(maxMutationIterations),
	maxCrossoverIterations(maxCrossoverIterations),
	maxPrimitiveSetSize(maxPrimitiveSetSize),
	angleEpsilon(angleEpsilon),
	availableManifoldTypes(getAvailableManifoldTypes(ms)),
	minDistanceBetweenParallelPlanes(minDistanceBetweenParallelPlanes)
{
	rndEngine.seed(rndDevice());
}

int lmu::PrimitiveSetCreator::getRandomPrimitiveIdx(const PrimitiveSet& ps) const
{
	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	return du(rndEngine, parmu_t{ 0, (int)ps.size() - 1 });
}

lmu::PrimitiveSet lmu::PrimitiveSetCreator::mutate(const PrimitiveSet& ps) const
{
	static std::discrete_distribution<int> dd{ mutationDistribution.begin(), mutationDistribution.end() };
	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	MutationType mt = (MutationType)dd(rndEngine);

	if (mt == MutationType::NEW || ps.empty())
	{
		std::cout << "Mutation New" << std::endl;
		return create();
	}
	else
	{
		auto newPS = ps;

		for (int i = 0; i < du(rndEngine, parmu_t{ 1, (int)maxMutationIterations }); ++i)
		{
			switch (mt)
			{
			case MutationType::REPLACE:
			{
				std::cout << "Mutation Replace" << std::endl;

				int idx = getRandomPrimitiveIdx(newPS);
				if (idx != -1)
				{
					auto newP = createPrimitive();
					newPS[idx] = newP.isNone() ? newPS[idx] : newP;
				}

				break;
			}
			case MutationType::MODIFY:
			{
				std::cout << "Mutation Modify" << std::endl;

				int idx = getRandomPrimitiveIdx(newPS);
				auto newP = mutatePrimitive(newPS[idx], angleEpsilon);
				newPS[idx] = newP.isNone() ? newPS[idx] : newP;

				break;
			}
			case MutationType::REMOVE:
			{
				std::cout << "Mutation Remove" << std::endl;

				//int idx = getRandomPrimitiveIdx(newPS);
				//newPS.erase(newPS.begin() + idx);

				break;
			}
			case MutationType::ADD:
			{
				std::cout << "Mutation Add" << std::endl;

				auto newP = createPrimitive();
				if (!newP.isNone())
					newPS.push_back(newP);

				break;
			}
			default:
				std::cout << "Warning: Unknown mutation type." << std::endl;
			}
		}

		return newPS;
	}
}

/*
std::vector<lmu::PrimitiveSet> lmu::PrimitiveSetCreator::crossover(const PrimitiveSet& ps1, const PrimitiveSet& ps2) const
{
std::cout << "Crossover" << std::endl;

static std::bernoulli_distribution db{};
using parmb_t = decltype(db)::param_type;

static std::uniform_int_distribution<> du{};
using parmu_t = decltype(du)::param_type;

PrimitiveSet newPS1 = ps1;
PrimitiveSet newPS2 = ps2;

for (int i = 0; i < du(rndEngine, parmu_t{ 1, (int)maxCrossoverIterations }); ++i)
{
bool intra = db(rndEngine, parmb_t{ intraCrossProb });

if (intra)
{
//TODO (if it makes sense).
}
else
{
if (!ps1.empty() && !ps2.empty())
{
int idx1 = getRandomPrimitiveIdx(ps1);
int idx2 = getRandomPrimitiveIdx(ps2);

if (idx1 != -1 && idx2 != -1)
{
newPS1[idx1] = ps2[idx2];
newPS2[idx2] = ps1[idx1];
}
}
}
}

return { newPS1, newPS2 };
}
*/

std::vector<lmu::PrimitiveSet> lmu::PrimitiveSetCreator::crossover(const PrimitiveSet& ps1, const PrimitiveSet& ps2) const
{
	std::cout << "Crossover" << std::endl;

	static std::bernoulli_distribution db{};
	using parmb_t = decltype(db)::param_type;

	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	PrimitiveSet newPS1 = ps1;
	PrimitiveSet newPS2 = ps2;

	for (int i = 0; i < du(rndEngine, parmu_t{ 1, (int)maxCrossoverIterations }); ++i)
	{
		bool intra = db(rndEngine, parmb_t{ intraCrossProb });

		if (intra)
		{
			//TODO (if it makes sense).
		}
		else
		{
			if (!ps1.empty() && !ps2.empty())
			{
				int idx1 = getRandomPrimitiveIdx(ps1);
				int idx2 = getRandomPrimitiveIdx(ps2);

				if (idx1 != -1 && idx2 != -1)
				{
					//newPS1[idx1] = ps2[idx2];
					//newPS2[idx2] = ps1[idx1];
					for (int j = idx2; j < std::min(newPS1.size(), ps2.size()); ++j) {
						newPS1[j] = ps2[j];
					}

					for (int j = idx1; j < std::min(ps1.size(), newPS2.size()); ++j) {
						newPS2[j] = ps1[j];
					}
				}
			}
		}
	}

	return { newPS1, newPS2 };
}

lmu::PrimitiveSet lmu::PrimitiveSetCreator::create() const
{
	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	static std::bernoulli_distribution db{};
	using parmb_t = decltype(db)::param_type;

	int setSize = du(rndEngine, parmu_t{ 1, (int)maxPrimitiveSetSize });

	PrimitiveSet ps;

	// Fill primitive set with randomly created primitives. 
	while (ps.size() < setSize)
	{
		//std::cout << "try to create primitive" << std::endl;
		auto p = createPrimitive();
		if (!p.isNone())
		{
			ps.push_back(p);

			//std::cout << "Added Primitive" << std::endl;
		}
		else
		{
			//std::cout << "Added None" << std::endl;
		}
	}

	//std::cout << "PS SIZE: " << ps.size() << std::endl;

	return ps;
}

std::string lmu::PrimitiveSetCreator::info() const
{
	return std::string();
}

lmu::ManifoldPtr lmu::PrimitiveSetCreator::getManifold(ManifoldType type, const Eigen::Vector3d& direction, 
	const ManifoldSet& alreadyUsed, double angleEpsilon, bool ignoreDirection, 
	const Eigen::Vector3d& point, double minimumPointDistance) const
{
	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	ManifoldSet candidates;
	const double cos_e = std::cos(angleEpsilon);

	// Filter manifold list.
	std::copy_if(ms.begin(), ms.end(), std::back_inserter(candidates),
		[type, &alreadyUsed, &direction, cos_e, ignoreDirection, &point, minimumPointDistance](const ManifoldPtr& m)
	{
		//std::cout << (direction.norm() || ignoreDirection) << " " << m->n.norm() << std::endl;

		return
			m->type == type &&												// same type.
			std::find_if(alreadyUsed.begin(), alreadyUsed.end(),			// not already used.
				[&m, minimumPointDistance](const ManifoldPtr& alreadyUsedM) {
					return lmu::manifoldsEqual(*m, *alreadyUsedM, 0.0001);
				}) == alreadyUsed.end() &&
			(ignoreDirection || std::abs(direction.dot(m->n)) > cos_e) &&	// same direction (or flipped).
			std::abs((point - m->p).dot(m->n)) > minimumPointDistance;		// distance between point and plane  
	});

	if (candidates.empty())
		return nullptr;

	return candidates[du(rndEngine, parmu_t{ 0, (int)candidates.size() - 1 })];
}

lmu::ManifoldPtr lmu::PrimitiveSetCreator::getPerpendicularPlane(const std::vector<ManifoldPtr>& planes, 
	const ManifoldSet& alreadyUsed, double angleEpsilon) const
{
	//std::cout << "perp ";

	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	ManifoldSet candidates;
	const double cos_e = std::cos(angleEpsilon);

	// Filter manifold list.
	std::copy_if(ms.begin(), ms.end(), std::back_inserter(candidates),
		[&alreadyUsed, &planes, cos_e](const ManifoldPtr& m)
	{
		if (m->type != ManifoldType::Plane)
		{
			return false;
		}

		if (std::find_if(alreadyUsed.begin(), alreadyUsed.end(), [&m](const ManifoldPtr& alreadyUsedM)
			{ return lmu::manifoldsEqual(*m, *alreadyUsedM, 0.0001); }) != alreadyUsed.end())
		{
			return false;
		}

		for (const auto& plane : planes)
		{
			if (std::abs(plane->n.dot(m->n)) >= cos_e) // enforce perpendicular direction.
				return false;
		}

		return true;

	});

	if (candidates.empty())
		return nullptr;

	//std::cout << "found ";


	return candidates[du(rndEngine, parmu_t{ 0, (int)candidates.size() - 1 })];
}

lmu::ManifoldPtr lmu::PrimitiveSetCreator::getParallelPlane(const ManifoldPtr& plane, const ManifoldSet & alreadyUsed, 
	double angleEpsilon, double minDistanceToParallelPlane) const
{
	auto foundPlane = getManifold(ManifoldType::Plane, plane->n, alreadyUsed, angleEpsilon, false, plane->p, minDistanceToParallelPlane);
	return foundPlane;
}

std::unordered_set<lmu::ManifoldType> lmu::PrimitiveSetCreator::getAvailableManifoldTypes(const ManifoldSet & ms) const
{
	std::unordered_set<lmu::ManifoldType> amt;

	std::transform(ms.begin(), ms.end(), std::inserter(amt, amt.begin()),
		[](const auto& m) -> lmu::ManifoldType { return m->type; });

	return amt;
}

lmu::PrimitiveType lmu::PrimitiveSetCreator::getRandomPrimitiveType() const
{
	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	auto n = du(rndEngine, parmu_t{ 0, (int)availableManifoldTypes.size() - 1 });
	auto it = std::begin(availableManifoldTypes);
	std::advance(it, n);

	switch (*it)
	{
	case ManifoldType::Plane:
		return PrimitiveType::Box;
	case ManifoldType::Cylinder:
		return PrimitiveType::Cylinder;
	default:
		return PrimitiveType::None;
	}
}

lmu::Primitive lmu::PrimitiveSetCreator::createPrimitive() const
{
	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	static std::bernoulli_distribution db{};
	using parmb_t = decltype(db)::param_type;

	const auto anyDirection = Eigen::Vector3d(0, 0, 0);

	auto primitiveType = getRandomPrimitiveType();
	//std::cout << "Primitive Type: " << (int)primitiveType << std::endl;
	auto primitive = Primitive::None();

	switch (primitiveType)
	{
	case PrimitiveType::Box:
	{
		ManifoldSet planes;

		auto plane = getManifold(ManifoldType::Plane, anyDirection, {}, 0.0, true);
		if (!plane)
			break;
		planes.push_back(plane);

		plane = getParallelPlane(plane, planes, angleEpsilon, minDistanceBetweenParallelPlanes);
		if (!plane)
			break;
		planes.push_back(plane);

		plane = getPerpendicularPlane(planes, planes, angleEpsilon);
		if (!plane)
			break;
		planes.push_back(plane);

		plane = getParallelPlane(plane, planes, angleEpsilon, minDistanceBetweenParallelPlanes);
		if (!plane)
			break;
		planes.push_back(plane);

		plane = getPerpendicularPlane(planes, planes, angleEpsilon);
		if (!plane)
			break;
		planes.push_back(plane);

		plane = getParallelPlane(plane, planes, angleEpsilon, minDistanceBetweenParallelPlanes);
		if (!plane)
			break;
		planes.push_back(plane);

		/*std::cout << "####################################" << std::endl;
		if (planes.size() == 6)
		{
			for (int i = 0; i < planes.size(); ++i)
			{
				std::cout
					<< "p: " << planes[i]->p.x() << " " << planes[i]->p.y() << " " << planes[i]->p.z()
					<< " n: " << planes[i]->n.x() << " " << planes[i]->n.y() << " " << planes[i]->n.z() << std::endl;
			}
		}*/

		primitive = createBoxPrimitive(planes);
	}
	break;

	case PrimitiveType::Cylinder:
	{
		auto cyl = getManifold(ManifoldType::Cylinder, anyDirection, {}, 0.0, true);
		if (cyl)
		{
			ManifoldSet planes;

			auto numPlanesToSelect = du(rndEngine, parmu_t{ 0, 2 });

			for (int i = 0; i < numPlanesToSelect; ++i)
			{
				auto p = getManifold(ManifoldType::Plane, cyl->n, planes, angleEpsilon);
				if (p)
					planes.push_back(p);
			}
			primitive = createCylinderPrimitive(cyl, planes);
		}
	}
	break;

	case PrimitiveType::Sphere:
	{
		auto sphere = getManifold(ManifoldType::Sphere, anyDirection, {}, 0.0, true);
		if (sphere)
		{
			primitive = createSpherePrimitive(sphere);
		}
	}
	break;
	}

	primitive.cutout = db(rndEngine, parmb_t{ 0.5 });
	//std::cout << "PC0: " << primitive.cutout << std::endl;


	return primitive;
}

lmu::Primitive lmu::PrimitiveSetCreator::mutatePrimitive(const Primitive& p, double angleEpsilon) const
{
	static std::uniform_int_distribution<> du{};
	using parmu_t = decltype(du)::param_type;

	static std::bernoulli_distribution db{};
	using parmb_t = decltype(db)::param_type;

	auto primitive = p;

	switch (primitive.type)
	{
	case PrimitiveType::Box:
	{
		// Find a new parallel plane to a randomly chosen plane (parallel planes come in pairs).
		int planePairIdx = du(rndEngine, parmu_t{ 0, 2 }) * 2;
		auto newPlane = getParallelPlane(p.ms[planePairIdx], p.ms, angleEpsilon, minDistanceBetweenParallelPlanes);
		if (newPlane)
		{
			auto newPlanes = ManifoldSet(p.ms);
			newPlanes[planePairIdx + 1] = newPlane;

			primitive = createBoxPrimitive(newPlanes);
		}

		break;
	}

	case PrimitiveType::Cylinder:

		ManifoldSet planes;
		auto numPlanesToSelect = du(rndEngine, parmu_t{ 0, 2 });
		auto cyl = p.ms[0]; //First element in manifold set is always the cylinder.
		for (int i = 0; i < numPlanesToSelect; ++i)
		{
			auto m = getManifold(ManifoldType::Plane, cyl->n, planes, angleEpsilon);
			if (m)
				planes.push_back(m);
		}

		primitive = createCylinderPrimitive(cyl, planes);

		break;
	}

	primitive.cutout = db(rndEngine, parmb_t{ 0.5 });
	//std::cout << "PC1: " << primitive.cutout << std::endl;

	return primitive;
}

// ==================== RANKER ====================

lmu::PrimitiveSetRanker::PrimitiveSetRanker(const PointCloud& pc, const ManifoldSet& ms, const PrimitiveSet& staticPrims, double distanceEpsilon, int maxPrimitiveSetSize) :
	pc(pc),
	ms(ms),
	staticPrimitives(staticPrims),
	distanceEpsilon(distanceEpsilon),
	bestRank(-std::numeric_limits<double>::max()),
	maxPrimitiveSetSize(maxPrimitiveSetSize)
{
}

lmu::PrimitiveSetRank lmu::PrimitiveSetRanker::rank(const PrimitiveSet& ps, bool ignoreStaticPrimitives) const
{
	return rank2(ps, ignoreStaticPrimitives);


	/*if (ps.empty()) {
		return -std::numeric_limits<double>::max();
	}

	CSGNode node = opUnion();
	for (const auto& p : ps)
	{
		if (p.cutout)
			node.addChild(opComp({geometry(p.imFunc)}));
		else
			node.addChild(geometry(p.imFunc));
	}

	if (!ignoreStaticPrimitives)
	{
		for (const auto& p : staticPrimitives)
			node.addChild(geometry(p.imFunc));
	}

	const double delta = 0.01;
	int validPoints = 0;
	int checkedPoints = 0;
	for (const auto& manifold : ms)
	{
		for (int i = 0; i < manifold->pc.rows(); ++i)
		{
			Eigen::Vector3d p = manifold->pc.block<1, 3>(i, 0);

			Eigen::Vector3d n = manifold->pc.block<1, 3>(i, 3);
			auto dg = node.signedDistanceAndGradient(p);
			double d = dg[0];
			Eigen::Vector3d g = dg.bottomRows(3);
			g.normalize();
			double dot = std::fabs(n.dot(g));

			validPoints += std::abs(d) < delta && dot > 0 && dot > 0.95;
			checkedPoints++;
		}
	}
	
	double g = 1.0;
	double geoScore = ((double)validPoints / (double)checkedPoints);

	double s = 0.2;
	double sizeScore = (double)ps.size() / (double)maxPrimitiveSetSize;

	//double p = 0.5;
	//double noteUsedScore = 1.0 - getCompleteUseScore(ms, ps);

	double r = g * geoScore - s * sizeScore; // -p * noteUsedScore;

	if (bestRank < r)
	{
		bestRank = r;
		bestPrimitives = ps;
	}

	return r;*/
}

#include <igl/opengl/glfw/Viewer.h>

Eigen::MatrixXd concatMatrices(const std::vector<Eigen::MatrixXd>& matrices)
{
	if (matrices.empty())
		return Eigen::MatrixXd(0, 0);

	size_t size = 0;
	for (const auto& m : matrices)
		size += m.rows();

	Eigen::MatrixXd res_m(size, matrices[0].cols());
	size_t row_offset = 0;
	for (size_t mat_idx = 0; mat_idx < matrices.size(); ++mat_idx) 
	{
		long cur_rows = matrices[mat_idx].rows();
		res_m.middleRows(row_offset, cur_rows) = matrices[mat_idx];
		row_offset += cur_rows;
	}

	return res_m;
}

void debug_visualize(lmu::Mesh& mesh, const lmu::ManifoldSet& planes, const std::vector<std::vector<Point_2>>& hulls,
	const std::vector<std::vector<Point_2>>& points_in_triangles, const lmu::PointCloud& pc, 
	const std::vector<std::vector<Point_2>>& rectangles)
{
	igl::opengl::glfw::Viewer viewer;
	std::vector<Eigen::MatrixXd> lines;

	std::cout << "HERE" << std::endl;

	std::cout << "Planes: " << planes.size() << " Hulls: " << hulls.size() << " Pts in Triangles: " << points_in_triangles.size() << std::endl;
	
	if (points_in_triangles.size() == planes.size())
	{
		for (int i = 0; i < planes.size(); ++i)
		{
			if (!hulls.empty() && !hulls[i].empty())
			{
				auto hull_3d = get3DPoints(planes[i], hulls[i]);
				Eigen::MatrixXd linesPerPlane(hull_3d.size(), 9);
				for (int j = 0; j < hull_3d.size() - 1; ++j)
				{
					linesPerPlane.row(j) << hull_3d[j].transpose(), hull_3d[j + 1].transpose(), Eigen::RowVector3d(1, 0, 0);
				}
				linesPerPlane.row(hull_3d.size() - 1) << hull_3d[0].transpose(), hull_3d[hull_3d.size() - 1].transpose(), Eigen::RowVector3d(1, 0, 0);

				lines.push_back(linesPerPlane);
			}

			if (!points_in_triangles[i].empty())
			{
				auto points_in_triangle = get3DPoints(planes[i], points_in_triangles[i]);
				lmu::PointCloud pc(points_in_triangle.size(), 6);
				for (int j = 0; j < points_in_triangle.size(); ++j)
				{
					pc.row(j) << points_in_triangle[j].transpose(), Eigen::RowVector3d(0, 1, 0);
				}
				viewer.data().add_points(pc.leftCols(3), pc.rightCols(3));
			}

			if (!rectangles.empty())
			{
				auto rectangle_points = get3DPoints(planes[i], rectangles[i]);
				for (int j = 0; j < rectangle_points.size(); j += 4)
				{
					/*
						Eigen::Vector2d p0 = (m_inv_rot * Eigen::Vector2d((double)x * raster_size, (double)y * raster_size)) + origin;
				Eigen::Vector2d p1 = (m_inv_rot * Eigen::Vector2d((double)(x + 1) * raster_size, (double)y * raster_size)) + origin;
				Eigen::Vector2d p2 = (m_inv_rot * Eigen::Vector2d((double)x * raster_size, (double)(y+1) * raster_size)) + origin;
				Eigen::Vector2d p3 = (m_inv_rot * Eigen::Vector2d((double)(x + 1) * raster_size, (double)(y+1) * raster_size)) + origin;

					*/

					Eigen::MatrixXd linesPerRectangle(4, 9);
					linesPerRectangle.row(0) << rectangle_points[j + 0].transpose(), rectangle_points[j + 1].transpose(), Eigen::RowVector3d(0, 0, 1);
					linesPerRectangle.row(1) << rectangle_points[j + 1].transpose(), rectangle_points[j + 3].transpose(), Eigen::RowVector3d(0, 0, 1);
					linesPerRectangle.row(2) << rectangle_points[j + 3].transpose(), rectangle_points[j + 2].transpose(), Eigen::RowVector3d(0, 0, 1);
					linesPerRectangle.row(3) << rectangle_points[j + 2].transpose(), rectangle_points[j + 0].transpose(), Eigen::RowVector3d(0, 0, 1);
					lines.push_back(linesPerRectangle);
				}
			}
		}
	}

	viewer.data().add_points(pc.leftCols(3), pc.rightCols(3));
	viewer.data().set_mesh(mesh.vertices, mesh.indices);

	viewer.data().lines = concatMatrices(lines);
	viewer.data().show_lines = true;
	viewer.data().point_size = 5.0;
	viewer.core.background_color = Eigen::Vector4f(1, 1, 1, 1);

	viewer.launch();
}

#include <concaveman.h>

std::vector<Point_2> get_concave_hull(const std::vector<Point_2>& pts, const std::vector<Point_2>& convex_hull)
{
	std::vector<std::array<double, 2>> convex_hull_trans;
	convex_hull_trans.reserve(convex_hull.size()); 
	for (const auto& p : convex_hull)
		convex_hull_trans.push_back({p.x(), p.y()});

	std::vector<std::array<double, 2>> pts_trans;
	pts_trans.reserve(pts.size());
	for (const auto& p : pts)
		pts_trans.push_back({ p.x(), p.y() });
	
	auto concave_hull = concaveman<double, 16>(pts_trans, convex_hull_trans, 2, 0.0001);

	std::vector<Point_2> concave_hull_res;
	concave_hull_res.reserve(concave_hull.size());
	for (const auto& p : concave_hull)
		concave_hull_res.push_back(Point_2(p[0], p[1]));
	
	return concave_hull_res;
}

double get_rasterized_area(double raster_size, const std::vector<Point_2>& pts, const std::vector<Point_2>& triangle_points, 
	std::vector<Point_2>& rectangles)
{
	auto c0 = Eigen::Vector2d(triangle_points[0].x(), triangle_points[0].y());
	auto c1 = Eigen::Vector2d(triangle_points[1].x(), triangle_points[1].y());
	auto c2 = Eigen::Vector2d(triangle_points[2].x(), triangle_points[2].y());

	auto cv01 = c1 - c0; 
	auto cv12 = c2 - c1; 
	auto cv02 = c2 - c0; 
	
	Eigen::Vector2d v0;
	Eigen::Vector2d v1;
	Eigen::Vector2d origin;

	// Get orthogonal vectors 

	double dot0 = std::abs(cv01.dot(cv12));
	double dot1 = std::abs(cv01.dot(cv02));
	double dot2 = std::abs(cv12.dot(cv02));

	if (dot0 < dot1)
	{
		if (dot0 < dot2) // dot0 wins
		{
			v0 = cv01; 
			v1 = cv12; 
			origin = c0;
		}
		else // dot2 wins
		{
			v0 = cv12;
			v1 = cv02;
			origin = c1;
		}
	}
	else
	{
		if (dot1 < dot2) // dot1 wins
		{
			v0 = cv01;
			v1 = cv02;
			origin = c0;

		}
		else // dot2 wins
		{
			v0 = cv12;
			v1 = cv02;
			origin = c1;
		}
	}


	// Create 2d rotation matrix
	double angle = std::atan2(v0.y(), v0.x());
	auto rot_m = Eigen::Rotation2Dd(angle).inverse().toRotationMatrix();

	//std::cout << "Angle: " << angle << std::endl;
		
	//std::cout << "V0: " << (rot_m * v0).normalized() << " V1: " << (rot_m * v1).normalized() << std::endl;


	int w = (int)std::ceil(v0.norm() / raster_size);
	int h = (int)std::ceil(v1.norm() / raster_size);

	//std::cout << "W: " << w << " H: " << h << std::endl;
	
	bool* grid = new bool[w*h];
	std::fill_n(grid, w*h, false);

	int counter = 0;
	for (const auto& p : pts)
	{
		auto pv = Eigen::Vector2d(p.x(), p.y()); 
		pv = rot_m * (pv - origin);	

		int x = (int)(pv.x() / raster_size);
		int y = (int)(pv.y() / raster_size);
				
		int idx = y * w + x;

		//std::cout << "X: " << x << "  Y: " << y << " IDX: " << idx << std::endl;

		counter += !grid[idx];
		grid[idx] = true;
	}
	
	auto m_inv_rot = rot_m.inverse();

	for (int x = 0; x < w; ++x)
	{
		for (int y = 0; y < h; ++y)
		{
			if (grid[y * w + x])
			{
				Eigen::Vector2d p0 = (m_inv_rot * Eigen::Vector2d((double)x * raster_size, (double)y * raster_size)) + origin;
				Eigen::Vector2d p1 = (m_inv_rot * Eigen::Vector2d((double)(x + 1) * raster_size, (double)y * raster_size)) + origin;
				Eigen::Vector2d p2 = (m_inv_rot * Eigen::Vector2d((double)x * raster_size, (double)(y+1) * raster_size)) + origin;
				Eigen::Vector2d p3 = (m_inv_rot * Eigen::Vector2d((double)(x + 1) * raster_size, (double)(y+1) * raster_size)) + origin;

				rectangles.push_back(Point_2(p0.x(), p0.y()));
				rectangles.push_back(Point_2(p1.x(), p1.y()));
				rectangles.push_back(Point_2(p2.x(), p2.y()));
				rectangles.push_back(Point_2(p3.x(), p3.y()));
			}
		}
	}

	delete[] grid;

	return counter * raster_size * raster_size;
}

lmu::PrimitiveSetRank lmu::PrimitiveSetRanker::rank2(const PrimitiveSet& ps, bool ignoreStaticPrimitives) const
{
	if (ps.empty()) 	
		return -std::numeric_limits<double>::max();
	
	// ===================== PART II: Computation of the Area Score =====================

	double summed_area = 0.0;
	double summed_point_area = 0.0;

	for (const auto& p : ps)
	{
		//size_t hash = p.hash(0);
		//{
		//	std::lock_guard<std::mutex> lk(lookupMutex);
		//
		//	if (primitiveAreaScoreLookup.count(hash) != 0)
		//	{
		//		area_score += primitiveAreaScoreLookup.at(hash);
		//		//std::cout << "Found in Lookup" << std::endl;
		//		continue;
		//	}
		//}

		if (p.type != PrimitiveType::Box)
		{
			std::cout << "Warning: primitive type is not a box." << std::endl;
			continue;
		}

		if (p.ms.size() != 6)
		{
			std::cout << "Warning: not exactly 6 planes available." << std::endl;
			continue;
		}

		auto mesh = createPolytope(Eigen::Affine3d::Identity(),
		{ p.ms[0]->p, p.ms[1]->p, p.ms[2]->p, p.ms[3]->p, p.ms[4]->p, p.ms[5]->p },
		{ p.ms[0]->n, p.ms[1]->n, p.ms[2]->n, p.ms[3]->n, p.ms[4]->n, p.ms[5]->n });

		if (mesh.empty())
		{
			std::cout << "Warning: mesh is empty." << std::endl;
			continue; // TODO: Check if this is correct.
		}

		if (mesh.indices.rows() != 12)
		{
			//std::cout << "Warning: mesh has != 12 triangles (" << mesh.indices.rows()  << ")" << std::endl;
			continue;
		}

		double per_primitive_point_area = 0.0;
		double per_primitive_area = 0.0;
		ManifoldSet selected_planes;
		std::vector<std::vector<Point_2>> hulls;
		std::vector<std::vector<Point_2>> points_in_triangles;
		std::vector<std::vector<Point_2>> rectangles;

		for (int i = 0; i < 12; ++i)
		{
			Eigen::Vector3d triangle[3] = {
				Eigen::Vector3d(mesh.vertices.row(mesh.indices.coeff(i,0))),
				Eigen::Vector3d(mesh.vertices.row(mesh.indices.coeff(i,1))),
				Eigen::Vector3d(mesh.vertices.row(mesh.indices.coeff(i,2)))
			};
			Eigen::Vector3d triangle_normal = (triangle[1] - triangle[0]).cross(triangle[2] - triangle[0]).normalized();

			// Find plane that has the same orientation as the triangle.
			int plane_idx = -1;
			double min_delta = std::numeric_limits<double>::max();
			for (int i = 0; i < p.ms.size(); ++i)
			{
				//double d = std::abs(triangle_normal.dot(p.ms[i]->n) - 1.0);

				// Measure distance of triangle points to plane surface and take max distance.
				double d = std::max(std::max(std::abs((triangle[0] - p.ms[i]->p).dot(p.ms[i]->n)),
					std::abs((triangle[1] - p.ms[i]->p).dot(p.ms[i]->n))), std::abs((triangle[2] - p.ms[i]->p).dot(p.ms[i]->n)));

				if (d < min_delta)
				{
					min_delta = d;
					plane_idx = i;
				}
			}
			if (plane_idx == -1)
			{
				std::cout << "Warning: plane is not defined." << std::endl;
				continue;
			}

			ManifoldPtr plane = p.ms[plane_idx];
			selected_planes.push_back(plane);

			// Project points of triangle and point cloud points on the plane.
			auto triangle_points_2d = get2DPoints(plane, triangle, 3);
			auto plane_points_2d = get2DPoints(plane);

			// Get all plane points that are inside triangle. 
			Polygon_2 triangle_polygon(triangle_points_2d.begin(), triangle_points_2d.end());
			if (!triangle_polygon.is_simple())
			{
				std::cout << "Warning polygon is not simple! " << triangle_polygon << std::endl;
				continue;
			}
			std::vector<Point_2> points_in_triangle_2d;
			for (const auto plane_point : plane_points_2d)
				if (triangle_polygon.bounded_side(plane_point) != CGAL::ON_UNBOUNDED_SIDE)
					points_in_triangle_2d.push_back(plane_point);

			points_in_triangles.push_back(points_in_triangle_2d);

			// Compute convex hull of point cloud points. 
			//std::vector<Point_2> convex_hull;
			//CGAL::convex_hull_2(points_in_triangle_2d.begin(), points_in_triangle_2d.end(), std::back_inserter(convex_hull));
			
			// Compute concave hull of point cloud points.
			//auto concave_hull = get_concave_hull(points_in_triangle_2d, convex_hull);			
			//hulls.push_back(concave_hull);

			// Compute area encompassed by points based on hull. 			
			// double hull_area = Polygon_2(concave_hull.begin(), concave_hull.end()).area();
			std::vector<Point_2> rectangles_per_triangle;
			double hull_area = get_rasterized_area(0.04, points_in_triangle_2d, triangle_points_2d, rectangles_per_triangle);
			rectangles.push_back(rectangles_per_triangle);

			double triangle_area = triangle_polygon.area();
			per_primitive_area += triangle_area;
			
			per_primitive_point_area += hull_area;
		}

		summed_area += per_primitive_area;
		summed_point_area += per_primitive_point_area;

		//if (per_primitive_point_area / per_primitive_area > 0.5)
		//{
		//	filtered_ps.push_back(p);
		//	summed_area += per_primitive_area;
		//	summed_point_area += per_primitive_point_area;
		//}
		
		//{
		//	std::lock_guard<std::mutex> lk(lookupMutex);
		//	primitiveAreaScoreLookup[hash] = per_primitive_area_coeff;
		//}
		
		//std::cout << "AREA COEFF: " << (per_primitive_point_area / per_primitive_area) << " GEO: " << ((double)validPoints / (double)checkedPoints)  << std::endl;
		
		//if (per_primitive_point_area / per_primitive_area >= 0.5)
		//	debug_visualize(mesh, selected_planes, hulls, points_in_triangles, pc, rectangles);
	}
	

	//const_cast<PrimitiveSet&>(ps) = filtered_ps;

	// ===================== PART I: Computation of the Geometry Score =====================

	const double delta = 0.0001;
	int validPoints = 0;
	int checkedPoints = 0;
	for (int i = 0; i < pc.rows(); ++i)
	{
		Eigen::Vector3d point = pc.block<1, 3>(i, 0);
		Eigen::Vector3d n = pc.block<1, 3>(i, 3);

		// Get distance to closest primitive. 
		double min_d = std::numeric_limits<double>::max();
		Eigen::Vector3d min_normal;
		for (const auto& p : ps)
		{
			auto dg = p.imFunc->signedDistanceAndGradient(point);
			double d = std::abs(dg[0]);
			//Eigen::Vector3d g = (1.0 - 2.0 * (double)p.cutout) * dg.bottomRows(3);

			Eigen::Vector3d g = dg.bottomRows(3);
			//if (p.cutout)
			//	g = -dg.bottomRows(3);

			if (min_d > d)
			{
				min_d = d;
				min_normal = g.normalized();
			}
		}

		//double dot = std::fabs(n.dot(min_normal));

		//std::cout << "MIN D: " << min_d << std::endl;

		//validPoints += (int)(min_d < delta && n.dot(min_normal) > 0);
		if (min_d < delta && n.dot(min_normal) > 0.9) validPoints++;
		checkedPoints++;
	}

	// ===================== PART III: Compute Weighted Sum =====================

	double s = 0.0;
	double size_score = (double)ps.size() / (double)maxPrimitiveSetSize;

	double g = 1.0;
	double geo_score = ((double)validPoints / (double)checkedPoints);

	double a = 1.0;
	double area_score = summed_point_area / summed_area;

	double r = a * area_score + g * geo_score - s * size_score;

	if (bestRank < r)
	{
		bestRank = r;
		bestPrimitives = ps;

		std::cout << "GEO SCORE: " << geo_score << " AREA SCORE: " << area_score << " SIZE SCORE: " << size_score << std::endl;
	}

	return r;
}

double lmu::PrimitiveSetRanker::getCompleteUseScore(const ManifoldSet& ms, const PrimitiveSet& ps) const
{
	std::unordered_set<ManifoldPtr> manifoldsInPS;
	for (const auto& p : ps)
		std::copy_if(p.ms.begin(), p.ms.end(), std::inserter(manifoldsInPS, manifoldsInPS.end()),
			[](const ManifoldPtr& m) {return m->type != ManifoldType::Plane; });

	return (double)manifoldsInPS.size() /
		(double)std::count_if(ms.begin(), ms.end(), [](const ManifoldPtr& m) {return m->type != ManifoldType::Plane; });
}

std::string lmu::PrimitiveSetRanker::info() const
{
	return std::string();
}

lmu::PrimitiveSet lmu::PrimitiveSetRanker::bestPrimitiveSet() const
{
	return bestPrimitives;
}

lmu::Primitive lmu::createBoxPrimitive(const ManifoldSet& planes)
{
	bool strictlyParallel = false;

	if (planes.size() != 6)
	{
		return Primitive::None();
	}

	std::vector<Eigen::Vector3d> p;
	std::vector<Eigen::Vector3d> n;
	ManifoldSet ms;
	for (int i = 0; i < planes.size() / 2; ++i)
	{
		auto newPlane1 = std::make_shared<Manifold>(*planes[i * 2]);
		auto newPlane2 = std::make_shared<Manifold>(*planes[i * 2 + 1]);

		Eigen::Vector3d p1 = newPlane1->p;
		Eigen::Vector3d n1 = newPlane1->n;
		Eigen::Vector3d p2 = newPlane2->p;
		Eigen::Vector3d n2 = newPlane2->n;

		// Check plane orientation and correct if necessary.
		double d1 = (p2 - p1).dot(n2) / n1.dot(n2);
		double d2 = (p1 - p2).dot(n1) / n2.dot(n1);
		if (d1 >= 0.0)
			newPlane1->n = newPlane1->n * -1.0;
		if (d2 >= 0.0)
			newPlane2->n = newPlane2->n * -1.0;

		ms.push_back(newPlane1);
		ms.push_back(newPlane2);

		n.push_back(newPlane1->n);

		if (strictlyParallel)
			n.push_back(newPlane1->n * -1.0);
		else
			n.push_back(newPlane2->n);

		p.push_back(newPlane1->p);
		p.push_back(newPlane2->p);
	}

	auto box = std::make_shared<IFPolytope>(Eigen::Affine3d::Identity(), p, n, "");
	if (box->empty())
	{
		return Primitive::None();
	}

	return Primitive(box, ms, PrimitiveType::Box);
}

lmu::Primitive lmu::createSpherePrimitive(const lmu::ManifoldPtr& m)
{
	if (!m || m->type != ManifoldType::Sphere)
		return Primitive::None();

	Eigen::Affine3d t = Eigen::Affine3d::Identity();
	t.translate(m->p);

	auto sphereIF = std::make_shared<IFSphere>(t, m->r.x(), ""); //TODO: Add name.

	return Primitive(sphereIF, { m }, PrimitiveType::Sphere);
}

lmu::Primitive lmu::createCylinderPrimitive(const ManifoldPtr& m, ManifoldSet& planes)
{
	switch (planes.size())
	{
	case 1: //estimate the second plane and go on as if there existed two planes.		
		planes.push_back(lmu::estimateSecondCylinderPlaneFromPointCloud(*m, *planes[0]));
	case 2:
	{
		// Cylinder height is distance between both parallel planes.
		// double height = std::abs((planes[0]->p - planes[1]->p).dot(planes[0]->n));

		// Get intersection points of cylinder ray and plane 0 and 1.		
		Eigen::Vector3d p0 = planes[0]->p;
		Eigen::Vector3d l0 = m->p;
		Eigen::Vector3d l = m->n;
		Eigen::Vector3d n = planes[0]->n;

		double d = (p0 - l0).dot(n) / l.dot(n);
		Eigen::Vector3d i0 = d * l + l0;

		p0 = planes[1]->p;
		n = planes[1]->n;
		d = (p0 - l0).dot(n) / l.dot(n);
		Eigen::Vector3d i1 = d * l + l0;

		double height = (i0 - i1).norm();
		Eigen::Vector3d pos = i0 + (0.5 * (i1 - i0));

		// Compute cylinder transform.
		Eigen::Matrix3d rot = getRotationMatrix(m->n);
		Eigen::Affine3d t = (Eigen::Affine3d)(Eigen::Translation3d(pos) * rot);

		// Create primitive. 
		auto cylinderIF = std::make_shared<IFCylinder>(t, m->r.x(), height, "");

		return Primitive(cylinderIF, { m, planes[0], planes[1] }, PrimitiveType::Cylinder);
	}
	case 0:	//Estimate cylinder height and center position using the point cloud only since no planes exist.
	{
		auto height = lmu::estimateCylinderHeightFromPointCloud(*m);
		auto pos = m->p;
		std::cout << "POS: " << m->p << std::endl;
		Eigen::Matrix3d rot = getRotationMatrix(m->n);
		Eigen::Affine3d t = (Eigen::Affine3d)(Eigen::Translation3d(pos) * rot);

		auto cylinderIF = std::make_shared<IFCylinder>(t, m->r.x(), height, "");

		return Primitive(cylinderIF, { m }, PrimitiveType::Cylinder);
	}
	default:
		return Primitive::None();
	}
}

lmu::PrimitiveSet lmu::extractCylindersFromCurvedManifolds(const ManifoldSet& manifolds, bool estimateHeight)
{
	PrimitiveSet primitives;

	for (const auto& m : manifolds)
	{
		if (m->type == ManifoldType::Cylinder)
		{
			double height = estimateCylinderHeightFromPointCloud(*m);			
			Eigen::Vector3d estimatedPos = m->p;

			Eigen::Vector3d up(0, 0, 1);
			Eigen::Vector3d f = m->n;
			Eigen::Vector3d r = (f).cross(up).normalized();
			Eigen::Vector3d u = (r).cross(f).normalized();

			Eigen::Matrix3d rot = Eigen::Matrix3d::Identity();
			rot <<
				r.x(), f.x(), u.x(),
				r.y(), f.y(), u.y(),
				r.z(), f.z(), u.z();

			Eigen::Affine3d t = (Eigen::Affine3d)(Eigen::Translation3d(/*m->p*/estimatedPos) * rot);

			auto cylinderIF = std::make_shared<IFCylinder>(t, m->r.x(), height, "");

			std::cout << "Cylinder: " << std::endl;
			std::cout << "Estimated Height: " << height << std::endl;
			std::cout << "----------------------" << std::endl;

			Primitive p(cylinderIF, { m }, PrimitiveType::Cylinder);

			if (!std::isnan(height) && !std::isinf(height))
			{
				primitives.push_back(p);
			}
			else
			{
				std::cout << "Filtered cylinder with nan or inf height. " << std::endl;
			}
		}
	}
	return primitives;
}

double lmu::estimateCylinderHeightFromPointCloud(const Manifold& m)
{
	// Get matrix for transform to identity rotation.
	double min_t = std::numeric_limits<double>::max();
	double max_t = -std::numeric_limits<double>::max();

	for (int i = 0; i < m.pc.rows(); ++i)
	{
		Eigen::Vector3d p = m.pc.row(i).leftCols(3).transpose();
		Eigen::Vector3d a = m.p;
		Eigen::Vector3d ab = m.n;
		Eigen::Vector3d ap = p - a;

		//A + dot(AP, AB) / dot(AB, AB) * AB
		Eigen::Vector3d proj_p = a + ap.dot(ab) / ab.dot(ab) * ab;

		// proj_p = m.p + m.n *t 
		double t = (proj_p.x() - m.p.x()) / m.n.x();

		min_t = t < min_t ? t : min_t;
		max_t = t > max_t ? t : max_t;
	}

	Eigen::Vector3d min_p = m.p + m.n * min_t;
	Eigen::Vector3d max_p = m.p + m.n * max_t;

	return (max_p - min_p).norm();
}


lmu::ManifoldPtr lmu::estimateSecondCylinderPlaneFromPointCloud(const Manifold& m, const Manifold& firstPlane)
{
	Eigen::Vector3d minPos = (Eigen::Vector3d)(m.pc.leftCols(3).colwise().minCoeff());
	Eigen::Vector3d maxPos = (Eigen::Vector3d)(m.pc.leftCols(3).colwise().maxCoeff());

	//Take the point of the point cloud's min-max points which is farer away from the first plane as the second plane's point.
	Eigen::Vector3d p = (firstPlane.p - minPos).norm() > (firstPlane.p - maxPos).norm() ? minPos : maxPos;

	auto secondPlane =
		std::make_shared<Manifold>(ManifoldType::Plane, p, -firstPlane.n, Eigen::Vector3d(0, 0, 0), PointCloud());

	return secondPlane;
}
