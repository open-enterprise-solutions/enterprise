#ifndef _MANAGER_REPORT_H__
#define _MANAGER_REPORT_H__

#include "dataReport.h"
#include "backend/wrapper/managerInfo.h"

class CReportManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CReportManager);
public:

	CReportManager(CMetaObjectReport* metaObject = nullptr);
	virtual ~CReportManager();

	virtual CMetaObjectCommonModule* GetModuleManager() const;

	virtual CMethodHelper* GetPMethods() const {  //�������� ������ �� ����� �������� ������� ���� ��������� � �������
		//PrepareNames(); 
		return m_methodHelper; 
	} 
	virtual void PrepareNames() const;                         //���� ����� ������������� ���������� ��� ������������� ���� ��������� � �������
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//����� ������

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	//types 
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:
	CMethodHelper* m_methodHelper;
	CMetaObjectReport* m_metaObject;
};

class CManagerExternalReport : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CManagerExternalReport);
public:
	CManagerExternalReport();
	virtual ~CManagerExternalReport();

	virtual CMethodHelper* GetPMethods() const {  //�������� ������ �� ����� �������� ������� ���� ��������� � �������
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;                         //���� ����� ������������� ���������� ��� ������������� ���� ��������� � �������
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//����� ������

protected:
	CMethodHelper* m_methodHelper;
};

#endif 