//***************************************************************************
//* Copyright (c) 2012 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#include <tclap/CmdLine.h>
#include "outputgenerator.h"
#include "test/unrolledlisttest.h"

//#define _RUN_TEST

std::string IntToStr(size_t value)
{
	std::stringstream ss;
	ss << value;
	return ss.str();
}

std::vector<std::pair<int, int> > ReadStageFile(const std::string & fileName)
{
	int count = 0;
	std::ifstream stageFile(fileName.c_str());
	if(!stageFile)
	{
		throw std::runtime_error("cannot open stage file");
	}

	if(!(stageFile >> count))
	{
		throw std::runtime_error("cannot read stage file");
	}

	if(count < 0)
	{
		throw std::runtime_error("number of stages must be nonnegative");
	}

	std::vector<std::pair<int, int> > ret(count);
	for(int i = 0; i < count; i++)
	{
		if(!(stageFile >> ret[i].first >> ret[i].second))
		{
			throw std::runtime_error("too few records in the stage file");
		}

		if(ret[i].first < 2)
		{
			throw std::runtime_error("vertex size in stage record must be at least 2");
		}

		if(ret[i].second < 0)
		{
			throw std::runtime_error("minimum branch size in stage record must be nonnegative");
		}
	}

	return ret;
}

std::vector<std::pair<int, int> > LooseStageFile()
{
	std::pair<int, int> stage[] = 
	{
		std::make_pair(30, 150),
		std::make_pair(100, 1000),
		std::make_pair(1000, 5000),		
		std::make_pair(5000, 15000)
	};

	return std::vector<std::pair<int, int> >(stage, stage + sizeof(stage) / sizeof(stage[0]));
}

std::vector<std::pair<int, int> > FineStageFile()
{
	std::pair<int, int> stage[] = 
	{
		std::make_pair(30, 150),
		std::make_pair(100, 1000),
		std::make_pair(1000, 2500),		
	};

	return std::vector<std::pair<int, int> >(stage, stage + sizeof(stage) / sizeof(stage[0]));
}

void PutProgressChr(size_t progress, SyntenyFinder::BlockFinder::State state)
{
	static size_t prev = 0;
	while(prev < progress)
	{
		prev++;
		std::cout << '.';
		std::cout.flush();
	}

	switch(state)
	{
	case SyntenyFinder::BlockFinder::start:
		prev = 0;
		std::cout << '[';
		break;
	case SyntenyFinder::BlockFinder::end:
		std::cout << "]" << std::endl;
		break;
	case SyntenyFinder::BlockFinder::run:
		;
	}
}

const std::string DELIMITER(80, '-');

void Handler(int sig)
{
	int entered = 0;
	if(entered++ == 0)
	{
		SyntenyFinder::TempFile::Cleanup();
	}

	exit(1);
}

int main(int argc, char * argv[])
{	
	signal(SIGINT, Handler);
	signal(SIGABRT, Handler);	
	signal(SIGTERM, Handler);

#ifdef _RUN_TEST
	SyntenyFinder::TestUnrolledList();
#endif

	std::stringstream parsets;		
	const std::string parameterSetNameArray[] = {"loose", "fine"};
	std::vector<std::string> parameterSetName(parameterSetNameArray, parameterSetNameArray + sizeof(parameterSetNameArray) / sizeof(parameterSetNameArray[0]));
	std::map<std::string, std::vector<std::pair<int, int> > > defaultParameters;
	defaultParameters["loose"] = LooseStageFile();
	defaultParameters["fine"] = FineStageFile();

	try
	{  
		TCLAP::CmdLine cmd("Program for finding syteny blocks in closely related genomes", ' ', "2.0.1");
		TCLAP::ValueArg<unsigned int> maxIterations("i",
			"maxiterations",
			"Maximum number of iterations during a stage of simplification, default = 4.",
			false,
			4,
			"integer",
			cmd);

		TCLAP::ValueArg<std::string> tempFileDir("t",
			"tempdir",
			"Directory where temporary files are stored",
			false,
			".",
			"dir name",
			cmd);

		TCLAP::ValueArg<std::string> stageFile("k",
			"stagefile",
			"File that contains manually chosen simplifications parameters. See USAGE file for more information.",
			false,
			"",
			"file name");

		TCLAP::SwitchArg graphFile("g",
			"graphfile",
			"Output resulting condensed de Bruijn graph (in dot format), default = not set.",
			cmd,
			false);

		TCLAP::SwitchArg sequencesFile("q",
			"sequencesfile",
			"Output sequences of synteny blocks (FASTA format), default = not set.",
			cmd,
			false);

		std::string description = std::string("Parameters set, used for the simplification. ") +
			std::string("Option \"loose\" produces fewer blocks, but they are larger (\"fine\" is opposite).");
		TCLAP::ValuesConstraint<std::string> allowedParametersVals(parameterSetName);
		TCLAP::ValueArg<std::string> parameters("s",
			"parameters",
			description,
			false,
			parameterSetName[0],
			&allowedParametersVals);

		TCLAP::ValueArg<unsigned int> minBlockSize("m",
			"minblocksize",
			"Minimum size of a synteny block, default value = 5000 BP.",
			false,
			5000,
			"integer",
			cmd);

		TCLAP::SwitchArg sharedOnly("a",
			"sharedonly",
			"Output only blocks that occur exactly once in each input sequence.",			
			cmd,
			false);

		TCLAP::SwitchArg inRAM("r",
			"inram",
			"Perform all computations in RAM, don't create temp files.",
			cmd,
			false);

		TCLAP::UnlabeledMultiArg<std::string> fileName("filenames",
			"FASTA file(s) with nucleotide sequences.",
			true,
			"file name",
			cmd);

		TCLAP::ValueArg<std::string> outFileDir("o",
			"outdir",
			"Directory where output files are written",
			false,
			".",
			"dir name",
			cmd);

		cmd.xorAdd(parameters, stageFile);
		cmd.parse(argc, argv);

		std::vector<std::pair<int, int> > stage;
		if(parameters.isSet())
		{
			stage = defaultParameters[parameters.getValue()];
		}
		else
		{
			stage = ReadStageFile(stageFile.getValue());
		}
		
		size_t totalSize = 0;
		std::vector<SyntenyFinder::FASTARecord> chrList;
		for(std::vector<std::string>::const_iterator it = fileName.begin(); it != fileName.end(); it++)
		{
			SyntenyFinder::FASTAReader reader(*it);
			if(!reader.IsOk())
			{
				throw std::runtime_error(("Cannot open file " + *it).c_str());
			}

			reader.GetSequences(chrList);			
		}
		
		for(size_t i = 0; i < chrList.size(); i++)
		{
			totalSize += chrList[i].GetSequence().size();
		}

		if(totalSize > SyntenyFinder::MAX_INPUT_SIZE)
		{
			throw std::runtime_error("Input is larger than 1 GB, can't proceed");
		}

		std::string tempDir = tempFileDir.isSet() ? tempFileDir.getValue() : outFileDir.getValue();		
		std::auto_ptr<SyntenyFinder::BlockFinder> finder(inRAM.isSet() ? new SyntenyFinder::BlockFinder(chrList) : new SyntenyFinder::BlockFinder(chrList, tempDir));

		for(size_t i = 0; i < stage.size(); i++)
		{
			std::cout << "Simplification stage " << i + 1 << " of " << stage.size() << std::endl;
			std::cout << "Enumerating vertices of the graph, then performing bulge removal..." << std::endl;
			finder->PerformGraphSimplifications(stage[i].first, stage[i].second, maxIterations.getValue(), PutProgressChr);
		}
		
		std::vector<SyntenyFinder::BlockInstance> blockList;
		std::cout << "Finding synteny blocks and generating the output..." << std::endl;
		size_t lastK = std::min(stage.back().first, static_cast<int>(minBlockSize.getValue()));
		finder->GenerateSyntenyBlocks(lastK, minBlockSize.getValue(), blockList, sharedOnly.getValue(), PutProgressChr);
		SyntenyFinder::OutputGenerator generator(chrList, blockList);

		SyntenyFinder::CreateDirectory(outFileDir.getValue());
		const std::string defaultCoordsFile = outFileDir.getValue() + "/block_coords.txt";
		const std::string defaultPermutationsFile = outFileDir.getValue() + "/genomes_permutations.txt";
		const std::string defaultCoverageReportFile = outFileDir.getValue() + "/coverage_report.txt";
		const std::string defaultSequencesFile = outFileDir.getValue() + "/blocks_sequences.fasta";
		const std::string defaultGraphFile = outFileDir.getValue() + "/de_bruijn_graph.dot";

		const std::string defaultCircosDir = outFileDir.getValue() + "/circos";
		const std::string defaultCircosFile = defaultCircosDir + "/circos.conf";
		const std::string defaultD3File = outFileDir.getValue() + "/d3_blocks_diagram.html";

		bool doOutput[] = {true, true, true, sequencesFile.isSet(), true, true};
		boost::function<void()> outFunction[] = 
		{
			boost::bind(&SyntenyFinder::OutputGenerator::ListChromosomesAsPermutations, boost::cref(generator), defaultPermutationsFile),
			boost::bind(&SyntenyFinder::OutputGenerator::GenerateReport, boost::cref(generator), defaultCoverageReportFile),
			boost::bind(&SyntenyFinder::OutputGenerator::ListBlocksIndices, boost::cref(generator), defaultCoordsFile),
			boost::bind(&SyntenyFinder::OutputGenerator::ListBlocksSequences, boost::cref(generator), defaultSequencesFile),		
			boost::bind(&SyntenyFinder::OutputGenerator::GenerateCircosOutput, boost::cref(generator), defaultCircosFile, defaultCircosDir),
			boost::bind(&SyntenyFinder::OutputGenerator::GenerateD3Output, boost::cref(generator), defaultD3File)
		};
		
		size_t length = sizeof(doOutput) / sizeof(doOutput[0]);
		for(size_t i = 0; i < length; i++)
		{
			if(doOutput[i])
			{
				outFunction[i]();
			}
		}

		if(graphFile.isSet())
		{
			std::stringstream buffer;
			finder->SerializeCondensedGraph(lastK, buffer, PutProgressChr);
			generator.OutputBuffer(defaultGraphFile, buffer.str());
		}

		std::cout.setf(std::cout.fixed);
		std::cout.precision(2);
		std::cout << "Time elapsed: " << double(clock()) / CLOCKS_PER_SEC << " seconds" << std::endl;
	} 
	catch (TCLAP::ArgException &e)
	{
		std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl; 
	}
	catch (std::runtime_error & e)
	{
		std::cerr << "error: " << e.what() << std::endl;
	}
	catch(...)
	{
		SyntenyFinder::TempFile::Cleanup();
	}

	return 0;
}
