#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#include "cli_CommandLineInterface.h"

#include "cli_Constants.h"
#include "sml_StringOps.h"
#include "sml_Names.h"

using namespace cli;
using namespace sml;

bool CommandLineInterface::ParseVersion(gSKI::IAgent* pAgent, std::vector<std::string>& argv) {
	unused(pAgent);
	unused(argv);

	return DoVersion();
}

bool CommandLineInterface::DoVersion() {

	char buf[kMinBufferSize];

	if (m_RawOutput) {
		m_ResultStream << m_KernelVersion.major << '.' << m_KernelVersion.minor;
	} else {
		AppendArgTag(sml_Names::kParamVersionMajor, sml_Names::kTypeInt, Int2String(m_KernelVersion.major, buf, kMinBufferSize));
		AppendArgTag(sml_Names::kParamVersionMinor, sml_Names::kTypeInt, Int2String(m_KernelVersion.minor, buf, kMinBufferSize));
	}
	return true;
}

