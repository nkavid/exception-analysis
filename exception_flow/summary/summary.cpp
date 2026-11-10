#include "exception_flow/summary/summary.hpp"

namespace exa::summary
{
clang::ssaf::SummaryName ExceptionFlowSummary::getSummaryName() const
{
  return summaryName();
}
} // namespace exa::summary
