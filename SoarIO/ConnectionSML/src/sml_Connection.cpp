/////////////////////////////////////////////////////////////////
// Connection class
//
// Author: Douglas Pearson, www.threepenny.net
// Date  : August 2004
//
// This class represents a logical connection between two entities that are communicating via SML.
// For example, an environment (the client) and the Soar kernel.
//
// The connection can be "embedded" which means both the client and the kernel are in the same process
// or it can be "remote" which means the client and the kernel are in different processes and possibly on different machines.
//
// Commands formatted as SML (a form of XML) are sent over this connection to issue commands etc.
//
// This class is actually an abstract base class, with specific implementations of this class
// being used to provide the different types of connections.
//
/////////////////////////////////////////////////////////////////

#include "sml_Connection.h"
#include "sml_ElementXML.h"
#include "sml_MessageSML.h"
#include "sml_EmbeddedConnection.h"
#include "sml_AnalyzeXML.h"

using namespace sml ;

/*************************************************************
* @brief Constructor
*************************************************************/
Connection::Connection()
{
	m_MessageID = 0 ;
	m_pUserData = NULL ;
}

/*************************************************************
* @brief Destructor
*************************************************************/
Connection::~Connection()
{
	// Delete all callback objects
	CallbackMapIter mapIter = m_CallbackMap.begin() ;

	while (mapIter != m_CallbackMap.end())
	{
		//char const*	pType = mapIter->first ;
		CallbackList*	pList = mapIter->second ;

		// Delete all callbacks in this list
		CallbackListIter iter = pList->begin() ;

		while (iter != pList->end())
		{
			Callback* pCallback = *iter ;
			iter++ ;
			delete pCallback ;
		}

		mapIter++ ;

		// Delete the list itself
		delete pList ;
	}
}


/*************************************************************
* @brief Creates a connection to a receiver that is embedded
*        within the same process.
*
* @param pLibraryName	The name of the library to load, without an extension (e.g. "ClientSML" or "KernelSML").  Case-sensitive (to support Linux).
*						This library will be dynamically loaded and connected to.
* @param pError			Pass in a pointer to an int and receive back an error code if there is a problem.
* @returns An EmbeddedConnection instance.
*************************************************************/
Connection* Connection::CreateEmbeddedConnection(char const* pLibraryName, ErrorCode* pError)
{
	// Set an initial error code and then replace it if something goes wrong.
	if (pError) *pError = Error::kNoError ;

	EmbeddedConnection* pConnection = EmbeddedConnection::CreateEmbeddedConnection() ;
	pConnection->AttachConnection(pLibraryName) ;

	// Report any errors
	if (pError) *pError = pConnection->GetLastError() ;

	return pConnection ;
}

/*************************************************************
* @brief Retrieve the response to the last call message sent.
*
*		 In an embedded situation, this result is always immediately available and the "wait" parameter is ignored.
*		 In a remote situation, if wait is false and the result is not immediately available this call returns false.
*
*		 The message is only required when the client is remote (because then there might be many responses waiting on the socket).
*		 A message can only be retrieved once, so a second call with the same ID will return NULL.
*		 Only the response to the last call message can be retrieved.
*
*		 The client is not required to call to get the result of a command it has sent.
*
*		 The implementation of this function will call ReceiveMessages() to get messages one at a time and process them.  Thus callbacks may be
*		 invoked while the client is blocked waiting for the particular response they requested.
*
*		 A response that is returned to the client through GetResultOfMessage() will not be passed to a callback
*		 function registered for response messages.  This allows a client to register a general function to check for
*		 any error messages and yet retrieve specific responses to calls that it is particularly interested in.
*
* @param pMsg	The original SML message that we wish to get a response from.
* @param wait	If true wait until the result is received (or we time out and report an error).
*
* @returns The message that is a response to pMsg or NULL if none is found.
*************************************************************/
ElementXML* Connection::GetResponse(ElementXML const* pXML, bool wait /* == true */)
{
	if (!pXML)
	{
		SetError(Error::kInvalidArgument) ;
		return NULL ;
	}

	char const* pID = pXML->GetAttribute(sml_Names::kID) ;

	if (pID == NULL)
	{
		SetError(Error::kArgumentIsNotSML) ;
		return NULL ;
	}

	return GetResponseForID(pID, wait) ;
}

/*************************************************************
* @brief Register a callback for a particular type of incoming message.
*
*		 Messages are currently one of:
*		 "call", "response" or "notify"
*		 A call is always paired to a response (think of this as a remote function call that returns a value)
*		 while a notify does not receive a response (think of this as a remote function call that does not return a value).
*		 This type is stored in the "doctype" attribute of the top level SML node in the message.
*		 NOTE: doctype's are case sensitive.
*
*		 You MUST register a callback for the "call" type of message.  This callback must return a "response" message which is then
*		 sent back over the connection.  Other callbacks should not return a message.
*		 Once the returned message has been sent it will be deleted.
*
*		 We will maintain a list of callbacks for a given type of SML document and call each in turn.
*		 Each callback on the list will be called in turn until one returns a non-NULL response.  No further callbacks
*		 will be called for that message.  This ensures that only one response is sent to a message.
*
* @param callback	The function to call when an incoming message is received (of the right type)
* @param pUserData	This data is passed to the callback.  It allows the callback to have some context to work in.  Can be NULL.
* @param pType		The type of message to register for (currently one of "call", "response" or "notify").
* @param addToEnd	If true add the callback to the end of the list (called last).  If false, add to front where it will be called first.
*
* @returns 0 if successful, otherwise an error code to indicate what went wrong.
*************************************************************/
void Connection::RegisterCallback(IncomingCallback callback, void* pUserData, char const* pType, bool addToEnd)
{
	ClearError() ;

	if (callback == NULL || pType == NULL)
	{
		SetError(Error::kInvalidArgument) ;
		return ;
	}

	// Create the callback object to be stored in the map
	Callback* pCallback = new Callback(this, callback, pUserData) ;

	// See if we have a list of callbacks for this type yet
	CallbackList* pList = m_CallbackMap[pType] ;

	if (!pList)
	{
		// Need to create the list
		pList = new CallbackList() ;
		m_CallbackMap[pType] = pList ;
	}
	
	// Add the callback to the list
	if (addToEnd)
		pList->push_back(pCallback) ;
	else
		pList->push_front(pCallback) ;
}

/*************************************************************
* @brief Removes a callback from the list of callbacks for a particular type of incoming message.
*
* @param callback	The function that was previously registered.  If NULL removes all callbacks for this type of message.
* @param pType		The type of message to unregister from (currently one of "call", "response" or "notify").
*
* @returns 0 if successful, otherwise an error code to indicate what went wrong.
*************************************************************/
void Connection::UnregisterCallback(IncomingCallback callback, char const* pType)
{
	ClearError() ;

	if (pType == NULL)
	{
		SetError(Error::kInvalidArgument) ;
		return ;
	}

	// See if we have a list of callbacks for this type
	CallbackList* pList = GetCallbackList(pType) ;

	if (pList == NULL)
	{
		SetError(Error::kCallbackNotFound) ;
		return ;
	}

	if (callback == NULL)
	{
		// Caller asked to delete all callbacks for this type
		delete pList ;
		m_CallbackMap[pType] = NULL ;

		return ;
	}

	// Walk the list of callbacks, deleting any objects that
	// match the callback function
	CallbackListIter iter = pList->begin() ;

	bool found = false ;

	while (iter != pList->end())
	{
		Callback* pCallback = *iter ;
		iter++ ;

		// See if this function matches the one we were passed
		// In which case, delete it.
		if (pCallback->getFunction() == callback)
		{
			delete pCallback ;
			found = true ;
		}
	}

	if (!found)
		SetError(Error::kCallbackNotFound) ;
}

/*************************************************************
* @brief Gets the list of callbacks associated with a given doctype (e.g. "call")
**************************************************************/
CallbackList* Connection::GetCallbackList(char const* pType)
{
	// We don't use the simpler m_CallbackMap[pType] because
	// doing this on a map has the nasty side-effect of adding an object to the map.
	// So the preferred method is to instead use find.
	CallbackMapIter iter = m_CallbackMap.find(pType) ;

	if (iter == m_CallbackMap.end())
		return NULL ;
	
	return iter->second ;
}

/*************************************************************
* @brief Invoke the list of callbacks matching the doctype of the incoming message.
*
* @param pIncomingMsg	The SML message that should be passed to the callbacks.
* @returns The response message (or NULL if there is no response from any callback).
*************************************************************/
ElementXML* Connection::InvokeCallbacks(ElementXML *pIncomingMsg)
{
	ClearError() ;

	MessageSML *pIncomingSML = (MessageSML*)pIncomingMsg ;

	// Check that we were passed a valid message.
	if (pIncomingMsg == NULL)
	{
		SetError(Error::kInvalidArgument) ;
		return NULL ;
	}

	// Retrieve the type of this message
	char const* pType = pIncomingSML->GetDocType() ;

	// Check that this message has a valid doc type (all valid SML do)
	if (pType == NULL)
	{
		SetError(Error::kNoDocType) ;
		return NULL ;
	}

	// Decide if this message is a "call" which requires a "response"
	bool isIncomingCall = pIncomingSML->IsCall() ;

	// See if we have a list of callbacks for this type
	CallbackList* pList = GetCallbackList(pType) ;

	// Nobody was interested in this type of message, so we're done.
	if (pList == NULL)
	{
		SetError(Error::kNoCallback) ;
		return NULL ;
	}

	CallbackListIter iter = pList->begin() ;

	// Walk the list of callbacks in turn until we reach
	// the end or one returns a message.
	while (iter != pList->end())
	{
		Callback* pCallback = *iter ;
		iter++ ;

		ElementXML* pResponse = pCallback->Invoke(pIncomingMsg) ;

		if (pResponse != NULL)
		{
			if (isIncomingCall)
				return pResponse ;

			// This callback was not for a call and should not return a result.
			// Delete the result and ignore it.
			pResponse->ReleaseRefOnHandle() ;
			pResponse = NULL ;
		}
	}

	// If this is a call, we must respond
	if (isIncomingCall)
		SetError(Error::kNoResponseToCall) ;

	// Nobody returned a response
	return NULL ;
}

/*************************************************************
* @brief Send a message and get the response.
*
* @param pAnalysis	This will be filled in with the analyzed response
* @param pMsg		The message to send
* @returns			True if got a reply and there were no errors.
*************************************************************/
bool Connection::SendMessageGetResponse(AnalyzeXML* pAnalysis, ElementXML* pMsg)
{
	// Send the command over
	SendMessage(pMsg);

	// Get the response
	ElementXML* pResponse = GetResponse(pMsg) ;

	if (!pResponse)
	{
		// We failed to get a reply when one was expected
		return false ;
	}

	// Analyze the response and return the analysis
	pAnalysis->Analyze(pResponse) ;

	delete pResponse ;

	// If the response is not SML, return false
	if (!pAnalysis->IsSML())
		return false ;

	// If we got an error, return false.
	if (pAnalysis->GetErrorTag())
		return false ;

	return true ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
*
* This family of commands are designed for access based on
* a named agent.  This agent's name is passed as the first
* parameter and then the other parameters define the details
* of which method to call for the agent.
* 
* Passing NULL for the agent name is valid and indicates
* that the command is not agent specific (e.g. "shutdown-kernel"
* would pass NULL).
*
* Uses SendMessageGetResponse() to do its work.
*
* @param pResponse		The response from the kernel to this command.
* @param pCommandName	The command to execute
* @param pAgentName		The name of the agent this command is going to (can be NULL -> implies going to top level of kernel)
* @param pParamName1	The name of the first argument for this command
* @param pParamVal1		The value of the first argument for this command
* @param rawOuput		If true, sends back a simple string form for the result which the caller will probably just print.
*						If false, sendds back a structured XML object that the caller can analyze and do more with.
* @returns	True if command was sent and received back without any errors (either in sending or in executing the command).
*************************************************************/
bool Connection::SendAgentCommand(AnalyzeXML* pResponse, char const* pCommandName, bool rawOutput)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName, rawOutput) ;

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
* See SendAgentCommand() above.
*************************************************************/
bool Connection::SendAgentCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pAgentName, bool rawOutput)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName, rawOutput) ;

	//add the agent name parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamAgent, pAgentName = 0);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
* See SendAgentCommand() above.
*************************************************************/
bool Connection::SendAgentCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pAgentName,
					char const* pParamName1, char const* pParamVal1,
					bool rawOutput)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName, rawOutput) ;

	//add the agent name parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamAgent, pAgentName);

	// add the other parameters
	AddParameterToSMLCommand(pMsg, pParamName1, pParamVal1);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
* See SendAgentCommand() above.
*************************************************************/
bool Connection::SendAgentCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pAgentName,
					char const* pParamName1, char const* pParamVal1,
					char const* pParamName2, char const* pParamVal2,
					bool rawOutput)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName, rawOutput) ;

	//add the agent name parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamAgent, pAgentName);

	// add the other parameters
	AddParameterToSMLCommand(pMsg, pParamName1, pParamVal1);
	AddParameterToSMLCommand(pMsg, pParamName2, pParamVal2);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
* See SendAgentCommand() above.
*************************************************************/
bool Connection::SendAgentCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pAgentName,
					char const* pParamName1, char const* pParamVal1,
					char const* pParamName2, char const* pParamVal2,
					char const* pParamName3, char const* pParamVal3,
					bool rawOutput)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName, rawOutput) ;

	//add the agent name parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamAgent, pAgentName);

	// add the other parameters
	AddParameterToSMLCommand(pMsg, pParamName1, pParamVal1);
	AddParameterToSMLCommand(pMsg, pParamName2, pParamVal2);
	AddParameterToSMLCommand(pMsg, pParamName3, pParamVal3);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
* See SendMessageGetResponse() for more.
*************************************************************/
bool Connection::SendClassCommand(AnalyzeXML* pResponse, char const* pCommandName)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName) ;

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
*
* @param pCommandName	The command to send
* @param pThisID		The id of the object (e.g. IAgent) whose method we are calling.
* @returns	An analyzed version of the reply
*************************************************************/
bool Connection::SendClassCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pThisID)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName) ;

	//add the 'this' pointer parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamThis, pThisID);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
*
* @param pCommandName	The command to send
* @param pThisID		The id of the object (e.g. IAgent) whose method we are calling.
* @param pParamName1	The name of the 1st parameter (after the 'this' param)
* @param pParamVal1		The value of the 1st parameter (after the 'this' param) (can be NULL if an optional param)
* @returns	An analyzed version of the reply
*************************************************************/
bool Connection::SendClassCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pThisID,
									char const* pParamName1, char const* pParamVal1)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName) ;

	//add the 'this' pointer parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamThis, pThisID);
	if (pParamVal1) AddParameterToSMLCommand(pMsg, pParamName1, pParamVal1);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
*
* @param pCommandName	The command to send
* @param pThisID		The id of the object (e.g. IAgent) whose method we are calling.
* @param pParamName1	The name of the 1st parameter (after the 'this' param)
* @param pParamVal1		The value of the 1st parameter (after the 'this' param) (can be NULL if optional param)
* @param pParamName2	The name of the 2nd parameter (after the 'this' param)
* @param pParamVal2		The value of the 2nd parameter (after the 'this' param) (can be NULL if optional param)
* @returns	An analyzed version of the reply
*************************************************************/
bool Connection::SendClassCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pThisID,
									char const* pParamName1, char const* pParamVal1,
									char const* pParamName2, char const* pParamVal2)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName) ;

	//add the 'this' pointer parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamThis, pThisID);

	// Note: If first param is missing, second must be ommitted too (normal optional param syntax)
	if (pParamVal1)				  AddParameterToSMLCommand(pMsg, pParamName1, pParamVal1);
	if (pParamVal1 && pParamVal2) AddParameterToSMLCommand(pMsg, pParamName2, pParamVal2);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}

/*************************************************************
* @brief Build an SML message and send it over the connection
*		 returning the analyzed version of the response.
*
* @param pCommandName	The command to send
* @param pThisID		The id of the object (e.g. IAgent) whose method we are calling.
* @param pParamName1	The name of the 1st parameter (after the 'this' param)
* @param pParamVal1		The value of the 1st parameter (after the 'this' param)
* @param pParamName2	The name of the 2nd parameter (after the 'this' param)
* @param pParamVal2		The value of the 2nd parameter (after the 'this' param)
* @param pParamName3	The name of the 3rd parameter (after the 'this' param)
* @param pParamVal3		The value of the 3rd parameter (after the 'this' param)
* @returns	An analyzed version of the reply
*************************************************************/
bool Connection::SendClassCommand(AnalyzeXML* pResponse, char const* pCommandName, char const* pThisID,
									char const* pParamName1, char const* pParamVal1,
									char const* pParamName2, char const* pParamVal2,
									char const* pParamName3, char const* pParamVal3)
{
	ElementXML* pMsg = CreateSMLCommand(pCommandName) ;

	//add the 'this' pointer parameter
	AddParameterToSMLCommand(pMsg, sml_Names::kParamThis, pThisID);
	
	if (pParamVal1)								AddParameterToSMLCommand(pMsg, pParamName1, pParamVal1);
	if (pParamVal1 && pParamVal2)				AddParameterToSMLCommand(pMsg, pParamName2, pParamVal2);
	if (pParamVal1 && pParamVal2 && pParamVal3) AddParameterToSMLCommand(pMsg, pParamName3, pParamVal3);

	bool result = SendMessageGetResponse(pResponse, pMsg) ;

	delete pMsg ;
	
	return result ;
}
