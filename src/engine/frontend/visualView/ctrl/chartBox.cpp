#include "chartBox.h"
#include "frontend/win/ctrls/charts/wxcharts.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueChartBox, IValueWindow);

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

CValueChartBox::CValueChartBox() : IValueWindow()
{
}

wxObject* CValueChartBox::Create(wxWindow* wxparent, IVisualHost *visualHost)
{
	/*wxMath2DPlotData chartData;
	wxSharedPtr<wxMath2DPlotOptions> options(new wxMath2DPlotOptions());
	options->GetCommonOptions().SetShowTooltips(false);

	wxVector<wxPoint2DDouble> points;
	auto pi = 3.1415926535897;
	auto tstart = -2 * pi;
	for (auto i = 0u; i < 100; i++)
	{
		auto x = tstart + 0.1*i;
		points.push_back(wxPoint2DDouble(x, cos(x)*sin(x)));
	}

	wxMath2DPlotDataset::ptr dataset(
		new wxMath2DPlotDataset(
			wxColor(255, 255, 255, 0),
			wxColor(250, 20, 20, 0x78),
			wxColor(0, 0, 0, 0xB8),
			points)
	);
	chartData.AddDataset(dataset);

	wxChartCtrl *m_chartBox = new wxMath2DPlotCtrl(wxparent, wxID_ANY, chartData, options);*/

	/*
	// Create the data for the area chart widget
	wxAreaChartData chartData;

	// Add a dataset
	wxVector<wxPoint2DDouble> points1;
	points1.push_back(wxPoint2DDouble(3, 3));
	points1.push_back(wxPoint2DDouble(3.5, 4));
	points1.push_back(wxPoint2DDouble(6, 2));
	points1.push_back(wxPoint2DDouble(7, -1));
	points1.push_back(wxPoint2DDouble(5, 0));
	points1.push_back(wxPoint2DDouble(4.5, 1.7));
	wxAreaChartDataset::ptr dataset1(new wxAreaChartDataset(points1));
	chartData.AddDataset(dataset1);

	// Create the area chart widget
	wxAreaChartCtrl* m_chartBox = new wxAreaChartCtrl(wxparent, wxID_ANY, chartData);
	*/
	/*
	// Create the data for the bar chart widget
	wxVector<wxString> labels;
	labels.push_back("January");
	labels.push_back("February");
	labels.push_back("March");
	labels.push_back("April");
	labels.push_back("May");
	labels.push_back("June");
	labels.push_back("July");
	wxChartsCategoricalData::ptr chartData = wxChartsCategoricalData::make_shared(labels);

	// Add the first dataset
	wxVector<wxDouble> points1;
	points1.push_back(3);
	points1.push_back(2.5);
	points1.push_back(1.2);
	points1.push_back(3);
	points1.push_back(6);
	points1.push_back(5);
	points1.push_back(1);
	wxChartsDoubleDataset::ptr dataset1(new wxChartsDoubleDataset("Dataset 1", points1));
	chartData->AddDataset(dataset1);

	// Add the second dataset
	wxVector<wxDouble> points2;
	points2.push_back(1);
	points2.push_back(1.33);
	points2.push_back(2.5);
	points2.push_back(2);
	points2.push_back(3);
	points2.push_back(1.8);
	points2.push_back(0.4);
	wxChartsDoubleDataset::ptr dataset2(new wxChartsDoubleDataset("Dataset 2", points2));
	chartData->AddDataset(dataset2);

	// Create the bar chart widget
	wxBarChartCtrl* m_chartBox = new wxBarChartCtrl(wxparent, wxID_ANY, chartData, m_pos, m_size);*/

	// Create a top-level panel to hold all the contents of the valueForm
	wxPanel* m_chartBox = new wxPanel(wxparent, wxID_ANY);

	// Create the data for the pie chart widget
	wxPieChartData::ptr chartData = wxPieChartData::make_shared();
	chartData->AppendSlice(wxChartSliceData(300, wxColor(0x4A46F7), "Red"));
	chartData->AppendSlice(wxChartSliceData(50, wxColor(0xBDBF46), "Green"));
	chartData->AppendSlice(wxChartSliceData(100, wxColor(0x5CB4FD), "Yellow"));
	chartData->AppendSlice(wxChartSliceData(40, wxColor(0xB19F94), "Grey"));
	chartData->AppendSlice(wxChartSliceData(120, wxColor(0x60534D), "Dark Grey"));

	// Create the pie chart widget
	wxPieChartCtrl* pieChartCtrl = new wxPieChartCtrl(m_chartBox, wxID_ANY, chartData,
		wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	// Create the legend widget
	wxChartsLegendData legendData(chartData->GetSlices());
	wxChartsLegendCtrl* legendCtrl = new wxChartsLegendCtrl(m_chartBox, wxID_ANY, legendData,
		wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	// Set up the sizer for the panel
	wxBoxSizer* panelSizer = new wxBoxSizer(wxHORIZONTAL);
	panelSizer->Add(pieChartCtrl, 1, wxEXPAND);
	panelSizer->Add(legendCtrl, 1, wxEXPAND);
	m_chartBox->SetSizer(panelSizer);

	return m_chartBox;
}

void CValueChartBox::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	wxWindow *m_chartBox = dynamic_cast<wxWindow *>(wxobject);
}

void CValueChartBox::OnSelected(wxObject* wxobject)
{
}

void CValueChartBox::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	wxWindow *m_chartBox = dynamic_cast<wxWindow *>(wxobject);

	if (m_chartBox)
	{
	}

	UpdateWindow(m_chartBox);
}

void CValueChartBox::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool CValueChartBox::LoadData(CMemoryReader &reader)
{
	return IValueWindow::LoadData(reader);
}

bool CValueChartBox::SaveData(CMemoryWriter &writer)
{
	return IValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(CValueChartBox, "chartbox", "container", string_to_clsid("CT_CHRB"));