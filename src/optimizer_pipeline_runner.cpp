#include "optimizer_pipeline_runner.h"
#include "helper.h"
#include "csgnode.h"
#include "csgnode_helper.h"
#include "optimizer_py.h"
#include "optimizer_red.h"
#include "optimizer_clustering.h"
#include "optimizer_ga.h"
#include "cit.h"

#include "mesh.h"
#include <igl/writeOBJ.h>

#include <fstream>

void sample_if_empty(const lmu::CSGNode& n, const lmu::PrimitiveCluster& primitives, double sampling_grid_size, lmu::CITSampling& cit_sampling)
{

	std::cout << "Generate CIT sets..." << std::endl;

	if (!cit_sampling.empty())
	{
		std::cout << "Already there. Done." << std::endl;
		return;
	}

	cit_sampling.in_sets.cits = lmu::generate_cits(n, sampling_grid_size, lmu::CITSGenerationOptions::INSIDE, primitives);	
	cit_sampling.out_sets.cits = lmu::generate_cits(n, sampling_grid_size, lmu::CITSGenerationOptions::OUTSIDE, primitives);
	
	cit_sampling.in = lmu::extract_points_from_cits(cit_sampling.in_sets.cits);
	cit_sampling.out = lmu::extract_points_from_cits(cit_sampling.out_sets.cits);

	cit_sampling.in_out = lmu::mergePointClouds({ cit_sampling.in, cit_sampling.out });

	std::cout << "Done." << std::endl;
}

void save_as_obj_mesh(const lmu::CSGNode& node, const std::string& path)
{
	auto mesh = lmu::computeMesh(node, Eigen::Vector3i(100, 100, 100));
	igl::writeOBJ(path, mesh.vertices, mesh.indices);
}

lmu::PipelineRunner::PipelineRunner(const std::string& input_config, const std::string& output_folder) :	
	output_folder(output_folder),
	params(input_config)
{
}

int lmu::PipelineRunner::run()
{
	auto pp = read_pipeline_params(params);
	CITSampling cit_sampling;
	
	// Load node.
	auto node = load(pp);
	if (node.operationType() == CSGNodeOperationType::Noop)
		return 1;

	std::cout << "Before: ";
	for (const auto& p : allDistinctFunctions(node))
		std::cout << p->name() << " ";
	std::cout << std::endl;

	// Find and remove primitive duplicates.
	node = filter_name_duplicates(node);
		
	std::cout << "After: ";
	for (const auto& p : allDistinctFunctions(node))
		std::cout << p->name() << " ";
	std::cout << std::endl;

	// Create output stat files.
	std::cout << "Create optimizer stat files..." << std::endl;
	std::ofstream opt_out(output_folder + "/opt_output.txt");
	opt_out << "# Input size: " << numNodes(node) << std::endl;
	opt_out << "# Input proximity: " << compute_local_proximity_score(node, pp.sampling_grid_size, empty_pc()) << std::endl;

	std::ofstream timings(output_folder + "/timings.ini");
	timings << "[Timings]" << std::endl;

	TimeTicker ticker;
	std::cout << "Done." << std::endl;

	// Remove Redundancies Before. 
	if (pp.use_redundancy_removal)
	{
		std::cout << "Remove Redundancies..." << std::endl;
		ticker.tick();

		if (pp.use_cit_points_for_redundancy_removal)
			sample_if_empty(node, {}, pp.sampling_grid_size, cit_sampling);
		
		node = remove_redundancies(node, pp.sampling_grid_size, cit_sampling.in_out);
		timings << "RemoveRedundancies=" << ticker.tick() << std::endl;
	}

	if (pp.save_meshes)
	{
		std::cout << "Save after red mesh..." << std::endl;
		save_as_obj_mesh(node, output_folder + "/after_red.obj");
		std::cout << "Done." << std::endl;
	}
	writeNode(node, output_folder + "/after_red.gv");

	opt_out << "# Before decompose size: " << numNodes(node) << std::endl;
	opt_out << "# Before decompose proximity: " << 
		compute_local_proximity_score(node, pp.sampling_grid_size, empty_pc()) << std::endl;

	std::cout << "Done." << std::endl;

	try
	{	
		if (pp.use_decomposition)
		{
			// Decompose. 
			std::cout << "Decompose..." << std::endl;
		
			ticker.tick();

			if (pp.use_cit_points_for_decomposition)
				sample_if_empty(node, {}, pp.sampling_grid_size, cit_sampling);

			node = optimize_with_decomposition(node, pp.sampling_grid_size, true, 
				cit_sampling.in_out, pp.use_cit_points_for_decomposition,
				[&pp, this, &opt_out, &timings, &cit_sampling](const CSGNode& node, const PrimitiveCluster& prims)
			{				
				return optimize(node, prims, pp, opt_out, timings);
			});

			timings << "DecompositionAndOpt=" << ticker.tick() << std::endl;
		}
		else
		{
			node = optimize(node, {}, pp, opt_out, timings);
		}	
	
		opt_out << "# Output size: " << numNodes(node) << std::endl;
		opt_out << "# Output proximity: " << 
			compute_local_proximity_score(node, pp.sampling_grid_size, empty_pc()) << std::endl;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "Decomposition / Optimization failed. Reason: " << ex.what() << std::endl;
		return 1;
	}	
	std::cout << "Done." << std::endl;

	if (node.type() == CSGNodeType::Operation && node.operationType() == CSGNodeOperationType::Noop)
	{
		std::cerr << "Something went wrong. Node is NoOp." << std::endl;
		return 1;
	}

	// Remove Redundancies Afterwards. 
	if (pp.use_redundancy_removal)
	{
		std::cout << "Remove Redundancies..." << std::endl;
		ticker.tick();

		if (pp.use_cit_points_for_redundancy_removal)
			sample_if_empty(node, {}, pp.sampling_grid_size, cit_sampling);

		node = remove_redundancies(node, pp.sampling_grid_size, cit_sampling.in_out);
		timings << "RemoveRedundanciesAfterwards=" << ticker.tick() << std::endl;
	}

	// Save results 
	writeNode(node, output_folder + "/output.gv");

	if (pp.save_meshes)
	{
		std::cout << "Save output mesh..." << std::endl;
		save_as_obj_mesh(node, output_folder + "/output.obj");
		std::cout << "Done." << std::endl;
	}

	opt_out.close();
	timings.close();

	return 0;
}
lmu::CSGNode lmu::PipelineRunner::load(const PipelineParams& pp)
{
	auto node = opNo();

	// Load CSG tree.
	std::cout << "Load CSG tree from '" << pp.tree_file << "'..." << std::endl;
	{
		try
		{
			node = fromJSONFile(pp.tree_file);

			node = to_binary_tree(node);
		}
		catch (const std::exception& ex)
		{
			std::cout << "Cannot load CSG tree from '" << pp.tree_file << "'. Reason: " << ex.what() << std::endl;
			return node;
		}
	}
	std::cout << "Done." << std::endl;

	// Save input node mesh.
	if (pp.save_meshes)
	{
		std::cout << "Save input mesh..." << std::endl;
		save_as_obj_mesh(node, output_folder + "/input.obj");
		std::cout << "Done." << std::endl;
	}

	//Save input node
	writeNode(node, output_folder + "/input.gv");

	return node;
}

lmu::CSGNode lmu::PipelineRunner::optimize(const CSGNode& node, const PrimitiveCluster& prims,
	const PipelineParams& pp, std::ofstream& opt_out, std::ofstream& timings)
{
	// Run Optimizer.
	std::cout << "Optimize..." << std::endl;

	opt_out << "# Before opt size: " << numNodes(node) << std::endl;

	TimeTicker opt_ticker;

	auto opt_node = opNo();

	if (pp.optimizer == "GA")
	{
		opt_node = optimize_with_ga(node, read_opt_ga_params(params), opt_out, prims).node;
	}
	else if (pp.optimizer == "Sampling.SetCover")
	{
		auto sp = read_opt_sampling_params(params);

		opt_node = optimize_pi_set_cover(node, sp.sampling_grid_size, sp.use_cit_points_for_pi_extraction,
			PythonInterpreter(sp.python_interpreter_path), prims, opt_out);

		opt_node = transform_to_diffs(lmu::to_binary_tree(opt_node));
	}
	else if (pp.optimizer == "Sampling.QuineMcCluskey")
	{
		auto sp = read_opt_sampling_params(params);
		opt_node = optimize_with_python(node, SimplifierMethod::SIMPY_SIMPLIFY_LOGIC,
			PythonInterpreter(sp.python_interpreter_path));

		opt_node = transform_to_diffs(lmu::to_binary_tree(opt_node));
	}
	else if (pp.optimizer == "Sampling.Espresso")
	{
		auto sp = read_opt_sampling_params(params);
		opt_node = optimize_with_python(node, SimplifierMethod::ESPRESSO,
			PythonInterpreter(sp.python_interpreter_path));

		opt_node = transform_to_diffs(lmu::to_binary_tree(opt_node));
	}
	else
	{
		throw std::runtime_error("Optimizer with name '" + pp.optimizer + "' does not exist.");
	}

	timings << "Optimization=" << opt_ticker.tick() << std::endl;

	return opt_node;
}

lmu::PipelineParams lmu::PipelineRunner::read_pipeline_params(const ParameterSet & params)
{
	PipelineParams pipeline_params;

	pipeline_params.optimizer = params.getStr("Pipeline", "Optimizer", "GA");
	pipeline_params.tree_file = params.getStr("Pipeline", "Tree", "tree.json");
	pipeline_params.sampling_grid_size = params.getDouble("Pipeline", "SamplingGridSize", 0.1);
	pipeline_params.save_meshes = params.getBool("Pipeline", "SaveMeshes", false);
	pipeline_params.use_decomposition = params.getBool("Pipeline", "UseDecomposition", true);
	pipeline_params.use_redundancy_removal = params.getBool("Pipeline", "UseRedundancyRemoval", true);
	pipeline_params.use_cit_points_for_decomposition = params.getBool("Pipeline", "UseCITPointsForDecomposition", false);
	pipeline_params.use_cit_points_for_redundancy_removal = params.getBool("Pipeline", "UseCITPointsForRedundancyRemoval", false);

	return pipeline_params;
}

lmu::SamplingParams lmu::PipelineRunner::read_opt_sampling_params(const ParameterSet & params)
{
	SamplingParams p;
	p.use_cit_points_for_pi_extraction = params.getBool("Sampling", "UseCITPointsForPiExtraction", false);
	
	p.sampling_grid_size = params.getDouble("Sampling", "SamplingGridSize", 0.1);
	p.python_interpreter_path = params.getStr("Sampling", "PythonInterpreterPath", "");

	return p;
}

lmu::OptimizerGAParams lmu::PipelineRunner::read_opt_ga_params(const ParameterSet& p)
{
	OptimizerGAParams opt_ga_params;

	opt_ga_params.ga_params.in_parallel = p.getBool("GA", "InParallel", true);
	opt_ga_params.ga_params.use_caching = p.getBool("GA", "UseCaching", true);
	opt_ga_params.ga_params.population_size = p.getInt("GA", "PopulationSize", 100);
	opt_ga_params.ga_params.num_best_parents = p.getInt("GA", "NumBestParents", 2);
	opt_ga_params.ga_params.mutation_rate = p.getDouble("GA", "MutationRate", 0.3);
	opt_ga_params.ga_params.crossover_rate = p.getDouble("GA", "CrossoverRate", 0.4);
	opt_ga_params.ga_params.tournament_k = p.getInt("GA", "TournamentK", 2);
	opt_ga_params.ga_params.max_iterations = p.getInt("GA", "MaxIterations", 100);
	opt_ga_params.ga_params.max_count = p.getInt("GA", "MaxCount", 10);
	opt_ga_params.ga_params.delta = p.getDouble("GA", "Delta", 0.0001);

	opt_ga_params.ranker_params.geo_score_strat =
		p.getStr("GA", "Ranker.GeoScoreStrategy", "Surface") == "Surface" ?
		GeoScoreStrategy::SURFACE_SAMPLES : GeoScoreStrategy::IN_OUT_SAMPLES;

	opt_ga_params.ranker_params.geo_score_weight = p.getDouble("GA", "Ranker.GeoScoreWeight", 20.0);
	opt_ga_params.ranker_params.size_score_weight = p.getDouble("GA", "Ranker.SizeScoreWeight", 2.0);
	opt_ga_params.ranker_params.prox_score_weight = p.getDouble("GA", "Ranker.ProxScoreWeight", 2.0);
	opt_ga_params.ranker_params.gradient_step_size = p.getDouble("GA", "Ranker.GradientStepSize", 0.0001);
	opt_ga_params.ranker_params.position_tolerance = p.getDouble("GA", "Ranker.PositionTolerance", 0.1);
	opt_ga_params.ranker_params.sampling_params.errorSigma = p.getDouble("GA", "Ranker.ErrorSigma", 0.00000001);
	opt_ga_params.ranker_params.sampling_params.samplingStepSize = p.getDouble("GA", "Ranker.SamplingStepSize", 0.1);
	opt_ga_params.ranker_params.sampling_params.maxDistance = p.getDouble("GA", "Ranker.MaxDistance", 0.1);
	opt_ga_params.ranker_params.max_sampling_points = p.getInt("GA", "Ranker.MaxSamplingPoints", 250);

	opt_ga_params.creator_params.create_new_prob = p.getDouble("GA", "Creator.CreateNewRandomProb", 0.3);
	opt_ga_params.creator_params.subtree_prob = p.getDouble("GA", "Creator.SubtreeProb", 0.3);
	opt_ga_params.creator_params.initial_population_dist = { 0.1,0.8,0.1 }; // TODO

	opt_ga_params.man_params.max_delta = 1.0; // TODO

	//int maxIterWithoutChange = p.getInt("StopCriterion", "MaxIterationsWithoutChange", 8 * numEdges(connectionGraph));
	//double changeDelta = p.getDouble("StopCriterion", "ChangeDelta", 0.01);

	return opt_ga_params;
}
