/*
 * Oreka -- A media capture and retrieval platform
 *
 * Copyright (C) 2005, orecx LLC
 *
 * http://www.orecx.com
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 * Please refer to http://www.gnu.org/copyleft/gpl.html
 *
 */
#pragma warning( disable: 4786 ) // disables truncated symbols in browse-info warning

#define _WINSOCKAPI_		// prevents the inclusion of winsock.h

#include "Utils.h"
#include "Metrics.h"
#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/sax/ErrorHandler.hpp>
#include <xercesc/sax/SAXParseException.hpp>
#include <xercesc/dom/DOMImplementation.hpp>
#include <xercesc/dom/DOMImplementationRegistry.hpp>
#include "serializers/DomSerializer.h"
#include "ConfigManager.h"
#include <fstream>

#define CONFIG_FILE_NAME "config.xml"
#define ETC_CONFIG_FILE_NAME "/etc/orkaudio/config.xml"

#ifdef WIN32
# define snprintf _snprintf
#endif

ConfigManager* ConfigManager::m_singleton = NULL;

ConfigManager* ConfigManager::Instance()
{
	if(m_singleton == NULL)
	{
		m_singleton = new ConfigManager();
	}
	return m_singleton;
}

class XmlErrorHandler: public xercesc::ErrorHandler
{
public:
	XmlErrorHandler() {}

	void warning(const xercesc::SAXParseException& exc) { throw exc; }
	void error(const xercesc::SAXParseException& exc) { throw exc; }
	void fatalError(const xercesc::SAXParseException& exc) { throw exc; }
	void resetErrors() {}
};


void ConfigManager::Initialize()
{
	Metering::Timer timed("ConfigManager.Initialize");
	OrkAprSubPool locPool;

	bool failed = false;
	m_configTopNode = NULL;
	apr_status_t ret;
	try
	{
		char* cfgFilename = NULL;
		char* cfgEnvPath = "";
		int cfgAlloc = 0;		
		ret = apr_env_get(&cfgEnvPath, "ORKAUDIO_CONFIG_PATH", AprLp);
		if(ret == APR_SUCCESS) {
			apr_dir_t* dir;
			ret = apr_dir_open(&dir, cfgEnvPath, AprLp);
			if(ret == APR_SUCCESS)
			{
				int len = 0;
				apr_dir_close(dir);
				len = strlen(cfgEnvPath)+1+strlen(CONFIG_FILE_NAME)+1;
				cfgFilename = (char*)malloc(len);
				if(cfgFilename) {
					cfgAlloc = 1;
					snprintf(cfgFilename, len, "%s/%s", cfgEnvPath, CONFIG_FILE_NAME);
				}
			}
		}

		if(!cfgFilename) {
			std::fstream file;
			file.open(CONFIG_FILE_NAME, std::fstream::in);

			if(file.is_open()){
				// config.xml exists in the current directory
				cfgFilename = (char*)CONFIG_FILE_NAME;
				file.close();
			}
			else
			{
				// config.xml could not be found in the current
				// directory, try to find it in system configuration directory
				cfgFilename = (char*)ETC_CONFIG_FILE_NAME;
			}
		}

        	XMLPlatformUtils::Initialize();

		// By default, the DOM document generated by the parser will be free() by the parser.
		// If we ever need to free the parser and the document separately, we need to do this:
		//		DOMNode *doc = parser->getDocument();
		//		...
		//		parser->adoptDocument();
		//		doc->release();
		//		...
		//		delete parser;
		XercesDOMParser *m_parser = new XercesDOMParser;
		XmlErrorHandler errhandler;
		m_parser->setErrorHandler(&errhandler);
		m_parser->parse(cfgFilename);
		DOMNode	*doc = NULL;
		doc = m_parser->getDocument();

		// XXX is it okay to free here?
		if(cfgAlloc) {
			free(cfgFilename);
		}

		if (doc)
		{
			DOMNode *firstChild = doc->getFirstChild();
			if (firstChild)
			{
				m_configTopNode = firstChild;
				m_config.DeSerializeDom(firstChild);

				/*
				// Write out config to a file
				DOMImplementation* impl =  DOMImplementationRegistry::getDOMImplementation(XStr("Core").unicodeForm());
				XERCES_CPP_NAMESPACE::DOMDocument* myDoc;
				   myDoc = impl->createDocument(
							   0,                    // root element namespace URI.
							   XStr("root").unicodeForm(),         // root element name
							   0);                   // document type object (DTD).
				m_config.SerializeDom(myDoc);
				CStdString toto = DomSerializer::DomNodeToString(myDoc);
				FILE* file = fopen("zzz.xml", "w");
				fwrite((PCSTR)toto,1,toto.GetLength(),file);
				fclose(file);
				*/
			}
			else
			{
				LOG4CXX_ERROR(LOG.configLog, CStdString("Could not parse config file:") + CONFIG_FILE_NAME);
				failed = true;
			}
		}
		else
		{
			LOG4CXX_WARN(LOG.configLog, CStdString("Could not find config file:") + CONFIG_FILE_NAME);
		}
	}
	catch (const CStdString& e)
	{
		LOG4CXX_ERROR(LOG.configLog, e);
		failed = true;
	}
	catch(const XMLException& e)
	{
		LOG4CXX_ERROR(LOG.configLog, e.getMessage());
		failed = true;
	}
	catch(const SAXParseException& e)
	{
		CStdString logMsg;
		logMsg.Format("config.xml error at line:%d, column:%d. (Use xmllint or xml editor to check the configuration)",  e.getLineNumber(), e.getColumnNumber());
		LOG4CXX_ERROR(LOG.configLog, logMsg);
		failed = true;
	}
	if (failed)
	{
		exit(0);
	}
}


void ConfigManager::AddConfigureFunction(ConfigureFunction configureFunction)
{
	m_configureFunctions.push_back(configureFunction);
	// Cal the external configure callback straight away
	configureFunction(m_configTopNode);
}
