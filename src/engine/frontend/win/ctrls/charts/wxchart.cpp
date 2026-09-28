/*
    Copyright (c) 2016-2019 Xavier Leclercq

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "wxchart.h"
#include "wxcharttooltip.h"
#include "wxchartmultitooltip.h"

wxChart::wxChart()
    : m_needsFit(true)
{
}

void wxChart::SetSize(const wxSize &size)
{
    DoSetSize(size);
    m_needsFit = true;
}

void wxChart::Draw(wxGraphicsContext &gc)
{
    DoDraw(gc, false);
}

void wxChart::ActivateElementsAt(const wxPoint &point)
{
    m_activePoint = point;
    m_hasActivePoint = true;
}

void wxChart::Fit()
{
    if (!m_needsFit)
    {
        return;
    }

    DoFit();

    m_needsFit = false;
}

void wxChart::DrawTooltips(wxGraphicsContext &gc)
{
    if (!m_hasActivePoint)
    {
        return;
    }

    // The elements under the mouse NOW — see m_activePoint.
    const wxSharedPtr<wxVector<const wxChartsElement*>> activeElements = GetActiveElements(m_activePoint);
    if (activeElements->size() == 1)
    {
        // If only one element is active draw a normal tooltip
        wxChartTooltip tooltip((*activeElements)[0]->GetTooltipPosition(),
            (*activeElements)[0]->GetTooltipProvider()->GetTooltipText());
        tooltip.Draw(gc);
    }
    else if (activeElements->size() > 1)
    {
        // If more than one element is active draw a multi-tooltip
        wxChartMultiTooltip multiTooltip((*activeElements)[0]->GetTooltipProvider()->GetTooltipTitle(),
            GetCommonOptions().GetMultiTooltipOptions());
        for (size_t j = 0; j < activeElements->size(); ++j)
        {
            wxChartTooltip tooltip((*activeElements)[j]->GetTooltipPosition(),
                (*activeElements)[j]->GetTooltipProvider());
            multiTooltip.AddTooltip(tooltip);
        }
        multiTooltip.Draw(gc);
    }
}
