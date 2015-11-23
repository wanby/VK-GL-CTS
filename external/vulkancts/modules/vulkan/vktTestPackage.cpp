/*-------------------------------------------------------------------------
 * Vulkan Conformance Tests
 * ------------------------
 *
 * Copyright (c) 2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and/or associated documentation files (the
 * "Materials"), to deal in the Materials without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Materials, and to
 * permit persons to whom the Materials are furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice(s) and this permission notice shall be
 * included in all copies or substantial portions of the Materials.
 *
 * The Materials are Confidential Information as defined by the
 * Khronos Membership Agreement until designated non-confidential by
 * Khronos, at which point this condition clause shall be removed.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
 *
 *//*!
 * \file
 * \brief Vulkan Test Package
 *//*--------------------------------------------------------------------*/

#include "vktTestPackage.hpp"

#include "tcuPlatform.hpp"
#include "tcuTestCase.hpp"
#include "tcuTestLog.hpp"

#include "vkPlatform.hpp"
#include "vkPrograms.hpp"
#include "vkBinaryRegistry.hpp"
#include "vkGlslToSpirV.hpp"
#include "vkSpirVAsm.hpp"

#include "deUniquePtr.hpp"

#include "vktInfo.hpp"
#include "vktApiTests.hpp"
#include "vktPipelineTests.hpp"
#include "vktBindingModelTests.hpp"
#include "vktSpvAsmTests.hpp"
#include "vktShaderLibrary.hpp"
#include "vktRenderPassTests.hpp"
#include "vktShaderRenderTests.hpp"

#include <vector>
#include <sstream>

namespace // compilation
{

vk::ProgramBinary* compileProgram (const glu::ProgramSources& source, glu::ShaderProgramInfo* buildInfo)
{
	return vk::buildProgram(source, vk::PROGRAM_FORMAT_SPIRV, buildInfo);
}

vk::ProgramBinary* compileProgram (const vk::SpirVAsmSource& source, vk::SpirVProgramInfo* buildInfo)
{
	return vk::assembleProgram(source, buildInfo);
}

template <typename InfoType, typename IteratorType>
vk::ProgramBinary* buildProgram (const std::string& casePath, IteratorType iter, vkt::Context* context, vk::BinaryCollection* progCollection)
{
	tcu::TestLog&					log			= context->getTestContext().getLog();
	const vk::ProgramIdentifier		progId		(casePath, iter.getName());
	const tcu::ScopedLogSection		progSection	(log, iter.getName(), "Program: " + iter.getName());
	de::MovePtr<vk::ProgramBinary>	binProg;
	InfoType						buildInfo;

	try
	{
		binProg	= de::MovePtr<vk::ProgramBinary>(compileProgram(iter.getProgram(), &buildInfo));
		log << buildInfo;
	}
	catch (const tcu::NotSupportedError& err)
	{
		// Try to load from cache
		const vk::BinaryRegistryReader	registry	(context->getTestContext().getArchive(), "vulkan/prebuilt");

		log << err << tcu::TestLog::Message << "Building from source not supported, loading stored binary instead" << tcu::TestLog::EndMessage;

		binProg = de::MovePtr<vk::ProgramBinary>(registry.loadProgram(progId));

		log << iter.getProgram();
	}
	catch (const tcu::Exception&)
	{
		// Build failed for other reason
		log << buildInfo;
		throw;
	}

	TCU_CHECK_INTERNAL(binProg);

	vk::ProgramBinary* returnBinary = binProg.get();

	progCollection->add(progId.programName, binProg);

	return returnBinary;
}

} // anonymous(compilation)

namespace vkt
{

using std::vector;
using de::UniquePtr;
using de::MovePtr;
using tcu::TestLog;

// TestCaseExecutor

class TestCaseExecutor : public tcu::TestCaseExecutor
{
public:
											TestCaseExecutor	(tcu::TestContext& testCtx);
											~TestCaseExecutor	(void);

	virtual void							init				(tcu::TestCase* testCase, const std::string& path);
	virtual void							deinit				(tcu::TestCase* testCase);

	virtual tcu::TestNode::IterateResult	iterate				(tcu::TestCase* testCase);

private:
	vk::BinaryCollection					m_progCollection;
	de::UniquePtr<vk::Library>				m_library;
	Context									m_context;

	TestInstance*							m_instance;			//!< Current test case instance
};

static MovePtr<vk::Library> createLibrary (tcu::TestContext& testCtx)
{
	return MovePtr<vk::Library>(testCtx.getPlatform().getVulkanPlatform().createLibrary());
}

TestCaseExecutor::TestCaseExecutor (tcu::TestContext& testCtx)
	: m_library		(createLibrary(testCtx))
	, m_context		(testCtx, m_library->getPlatformInterface(), m_progCollection)
	, m_instance	(DE_NULL)
{
}

TestCaseExecutor::~TestCaseExecutor (void)
{
	delete m_instance;
}

void TestCaseExecutor::init (tcu::TestCase* testCase, const std::string& casePath)
{
	const TestCase*			vktCase		= dynamic_cast<TestCase*>(testCase);
	tcu::TestLog&			log			= m_context.getTestContext().getLog();
	vk::SourceCollections	sourceProgs;

	DE_UNREF(casePath); // \todo [2015-03-13 pyry] Use this to identify ProgramCollection storage path

	if (!vktCase)
		TCU_THROW(InternalError, "Test node not an instance of vkt::TestCase");

	m_progCollection.clear();
	vktCase->initPrograms(sourceProgs);

	for (vk::GlslSourceCollection::Iterator progIter = sourceProgs.glslSources.begin(); progIter != sourceProgs.glslSources.end(); ++progIter)
	{
		vk::ProgramBinary* binProg = buildProgram<glu::ShaderProgramInfo, vk::GlslSourceCollection::Iterator>(casePath, progIter, &m_context, &m_progCollection);

		try
		{
			std::ostringstream disasm;

			vk::disassembleSpirV(binProg->getSize(), binProg->getBinary(), &disasm);

			log << TestLog::KernelSource(disasm.str());
		}
		catch (const tcu::NotSupportedError& err)
		{
			log << err;
		}
	}

	for (vk::SpirVAsmCollection::Iterator asmIterator = sourceProgs.spirvAsmSources.begin(); asmIterator != sourceProgs.spirvAsmSources.end(); ++asmIterator)
	{
		buildProgram<vk::SpirVProgramInfo, vk::SpirVAsmCollection::Iterator>(casePath, asmIterator, &m_context, &m_progCollection);
	}

	DE_ASSERT(!m_instance);
	m_instance = vktCase->createInstance(m_context);
}

void TestCaseExecutor::deinit (tcu::TestCase*)
{
	delete m_instance;
	m_instance = DE_NULL;
}

tcu::TestNode::IterateResult TestCaseExecutor::iterate (tcu::TestCase*)
{
	DE_ASSERT(m_instance);

	const tcu::TestStatus	result	= m_instance->iterate();

	if (result.isComplete())
	{
		// Vulkan tests shouldn't set result directly
		DE_ASSERT(m_context.getTestContext().getTestResult() == QP_TEST_RESULT_LAST);
		m_context.getTestContext().setTestResult(result.getCode(), result.getDescription().c_str());
		return tcu::TestNode::STOP;
	}
	else
		return tcu::TestNode::CONTINUE;
}

// ShaderLibrary-based GLSL tests

class GlslGroup : public tcu::TestCaseGroup
{
public:
	GlslGroup (tcu::TestContext& testCtx)
		: tcu::TestCaseGroup(testCtx, "glsl", "GLSL shader execution tests")
	{
	}

	void init (void)
	{
		static const struct
		{
			const char*		name;
			const char*		description;
		} s_es310Tests[] =
		{
			{ "arrays",						"Arrays"					},
			{ "conditionals",				"Conditional statements"	},
			{ "constant_expressions",		"Constant expressions"		},
			{ "constants",					"Constants"					},
			{ "conversions",				"Type conversions"			},
			{ "functions",					"Functions"					},
			{ "linkage",					"Linking"					},
			{ "scoping",					"Scoping"					},
			{ "swizzles",					"Swizzles"					},
		};

		for (int ndx = 0; ndx < DE_LENGTH_OF_ARRAY(s_es310Tests); ndx++)
			addChild(createShaderLibraryGroup(m_testCtx,
											  s_es310Tests[ndx].name,
											  s_es310Tests[ndx].description,
											  std::string("vulkan/glsl/es310/") + s_es310Tests[ndx].name + ".test").release());
	}
};

// TestPackage

TestPackage::TestPackage (tcu::TestContext& testCtx)
	: tcu::TestPackage(testCtx, "dEQP-VK", "dEQP Vulkan Tests")
{
}

TestPackage::~TestPackage (void)
{
}

tcu::TestCaseExecutor* TestPackage::createExecutor (void) const
{
	return new TestCaseExecutor(m_testCtx);
}

void TestPackage::init (void)
{
	addChild(createInfoTests			(m_testCtx));
	addChild(api::createTests			(m_testCtx));
	addChild(pipeline::createTests		(m_testCtx));
	addChild(BindingModel::createTests	(m_testCtx));
	addChild(SpirVAssembly::createTests	(m_testCtx));
	addChild(new GlslGroup				(m_testCtx));
	addChild(createRenderPassTests		(m_testCtx));
	addChild(sr::createTests			(m_testCtx));
}

} // vkt