#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#include "cli_CommandLineInterface.h"

#include <fstream>

#include "cli_GetOpt.h"
#include "cli_Constants.h"
#include "cli_CommandData.h"

#include "sml_Names.h"

#include "IgSKI_Agent.h"

using namespace cli;
using namespace sml;

bool CommandLineInterface::ParseLog(gSKI::IAgent* pAgent, std::vector<std::string>& argv) {
	static struct GetOpt::option longOptions[] = {
		{"add",			0, 0, 'a'},
		{"append",		0, 0, 'A'},
		{"close",		0, 0, 'c'},
		{"disable",		0, 0, 'd'},
		{"existing",	0, 0, 'e'},
		{"off",			0, 0, 'd'},
		{"query",		0, 0, 'q'},
		{0, 0, 0, 0}
	};

	OPTION_LOG operation = OPTION_LOG_NEW;

	for (;;) {
		int option = m_pGetOpt->GetOpt_Long(argv, "aAcdeoq", longOptions, 0);
		if (option == -1) break;

		switch (option) {
			case 'a':
				operation = OPTION_LOG_ADD;
				break;
			case 'c':
			case 'd':
			case 'o':
				operation = OPTION_LOG_CLOSE;
				break;
			case 'e':
			case 'A':
				operation = OPTION_LOG_NEWAPPEND;
				break;
			case 'q':
				operation = OPTION_LOG_QUERY;
				break;
			case '?':
				return SetError(CLIError::kUnrecognizedOption);
			default:
				return SetError(CLIError::kGetOptError);
		}
	}
	
	std::string toAdd;
	std::string filename;
	std::vector<std::string>::iterator iter = argv.begin();

	switch (operation) {
		case OPTION_LOG_ADD:
			// no less than one argument
			if (m_pGetOpt->GetAdditionalArgCount() < 1) return SetError(CLIError::kTooFewArgs);

			// combine all args
			for (int i = 0; i < m_pGetOpt->GetOptind(); ++i) {
				++iter;
			}
			while (iter != argv.end()) {
				toAdd += *iter;
				toAdd += ' ';
				++iter;
			}
			break;

		case OPTION_LOG_NEW:
			// no more than one argument
			if (m_pGetOpt->GetAdditionalArgCount() > 1) return SetError(CLIError::kTooManyArgs);
			if (m_pGetOpt->GetAdditionalArgCount() == 1) filename = argv[1];
			break;

		case OPTION_LOG_NEWAPPEND:
			// exactly one argument
			if (m_pGetOpt->GetAdditionalArgCount() > 1) return SetError(CLIError::kTooManyArgs);
			if (m_pGetOpt->GetAdditionalArgCount() < 1) return SetError(CLIError::kTooFewArgs);
			filename = argv[2];
			break;

		case OPTION_LOG_CLOSE:
		case OPTION_LOG_QUERY:
			// no arguments
			if (m_pGetOpt->GetAdditionalArgCount()) return SetError(CLIError::kTooManyArgs);
			break;

		default:
			return SetError(CLIError::kInvalidOperation);
	}

	return DoLog(pAgent, operation, filename, toAdd);
}

bool CommandLineInterface::DoLog(gSKI::IAgent* pAgent, OPTION_LOG operation, const std::string& filename, const std::string& toAdd) {
	if (!RequireAgent(pAgent)) return false;

	std::ios_base::openmode mode = std::ios_base::out;

	switch (operation) {
		case OPTION_LOG_NEWAPPEND:
			mode |= std::ios_base::app;
			// falls through

		case OPTION_LOG_NEW:
			if (filename.size() == 0) break;
			if (m_pLogFile) return SetError(CLIError::kLogAlreadyOpen);
			m_pLogFile = new std::ofstream(filename.c_str(), mode);
			if (!m_pLogFile) return SetError(CLIError::kLogOpenFailure);
			pAgent->AddPrintListener(gSKIEVENT_PRINT, &m_LogPrintHandler);
			m_LogFilename = filename;
			break;

		case OPTION_LOG_ADD:
			if (!m_pLogFile) return SetError(CLIError::kLogNotOpen);
			(*m_pLogFile) << toAdd << std::endl;
			return true;

		case OPTION_LOG_CLOSE:
			if (!m_pLogFile) return SetError(CLIError::kLogNotOpen);
			pAgent->RemovePrintListener(gSKIEVENT_PRINT, &m_LogPrintHandler);
	
			(*m_pLogFile) << "Log file closed." << std::endl;

			delete m_pLogFile;
			m_pLogFile = 0;
			m_LogFilename.clear();
			break;

		case OPTION_LOG_QUERY:
			break;
		default:
			return SetError(CLIError::kInvalidOperation);
	}

	// Query at end of successful command, or by default (but not on _ADD)
	if (m_RawOutput) {
		m_ResultStream << m_pLogFile ? "Log file '" + m_LogFilename + "' opened." : "Log file closed.";

	} else {
		const char* setting = m_pLogFile ? sml_Names::kTrue : sml_Names::kFalse;
		AppendArgTag(sml_Names::kParamLogSetting, sml_Names::kTypeBoolean, setting);

		if (m_LogFilename.size()) AppendArgTag(sml_Names::kParamFilename, sml_Names::kTypeString, m_LogFilename.c_str());
	}

	return true;
}

