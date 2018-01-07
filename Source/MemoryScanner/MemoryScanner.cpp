#include "MemoryScanner.h"

#include "../Common/CommonUtils.h"
#include "../DolphinProcess/DolphinAccessor.h"

MemScanner::MemScanner() : m_resultsConsoleAddr(std::vector<u32>())
{
}

MemScanner::~MemScanner()
{
  delete[] m_scanRAMCache;
}

Common::MemOperationReturnCode MemScanner::firstScan(const MemScanner::ScanFiter filter,
                                                     const std::string& searchTerm1,
                                                     const std::string& searchTerm2)
{
  m_scanRAMCache = nullptr;
  u32 ramSize = 0;
  if (DolphinComm::DolphinAccessor::isMEM2Present())
  {
    ramSize = Common::MEM1_SIZE + Common::MEM2_SIZE;
    m_scanRAMCache = new char[ramSize - 1];
    if (!DolphinComm::DolphinAccessor::readFromRAM(Common::dolphinAddrToOffset(Common::MEM2_START),
                                                   m_scanRAMCache + Common::MEM1_SIZE,
                                                   Common::MEM2_SIZE - 1, false))
    {
      delete[] m_scanRAMCache;
      return Common::MemOperationReturnCode::operationFailed;
    }
  }
  else
  {
    ramSize = Common::MEM1_SIZE;
    m_scanRAMCache = new char[ramSize - 1];
  }

  if (!DolphinComm::DolphinAccessor::readFromRAM(Common::dolphinAddrToOffset(Common::MEM1_START),
                                                 m_scanRAMCache, Common::MEM1_SIZE - 1, false))
  {
    delete[] m_scanRAMCache;
    return Common::MemOperationReturnCode::operationFailed;
  }

  if (filter == ScanFiter::unknownInitial)
  {
    m_resultCount = (ramSize - Common::getSizeForType(m_memType, static_cast<size_t>(1)));
    m_wasUnknownInitialValue = true;
    m_memSize = 1;
    m_scanStarted = true;
    return Common::MemOperationReturnCode::OK;
  }

  bool m_wasUnknownInitialValue = false;
  Common::MemOperationReturnCode scanReturn = Common::MemOperationReturnCode::OK;
  size_t termActualLength = 0;
  size_t termMaxLength = 0;
  if (m_memType == Common::MemType::type_string)
  {
    // This is just to have the string formatted with the appropriate length, byte arrays don't need
    // this because they get copied byte per byte
    termMaxLength = searchTerm1.length();
  }
  else
  {
    // Have no restriction on the length for the rest
    termMaxLength = ramSize;
  }
  char* memoryToCompare1 = Common::formatStringToMemory(scanReturn, termActualLength, searchTerm1,
                                                        m_memBase, m_memType, termMaxLength);
  if (scanReturn != Common::MemOperationReturnCode::OK)
    return scanReturn;

  char* memoryToCompare2 = nullptr;
  if (filter == ScanFiter::between)
  {
    memoryToCompare2 = Common::formatStringToMemory(scanReturn, termActualLength, searchTerm2,
                                                    m_memBase, m_memType, ramSize);
    if (scanReturn != Common::MemOperationReturnCode::OK)
      return scanReturn;
  }

  bool withBSwap = Common::shouldBeBSwappedForType(m_memType);

  m_memSize = Common::getSizeForType(m_memType, termActualLength);

  char* noOffset = new char[m_memSize];
  std::memset(noOffset, 0, m_memSize);

  for (u32 i = 0; i < (ramSize - m_memSize); ++i)
  {
    char* memoryCandidate = &m_scanRAMCache[i];
    bool isResult = false;
    switch (filter)
    {
    case ScanFiter::exact:
    {
      if (m_memType == Common::MemType::type_string || m_memType == Common::MemType::type_byteArray)
        isResult = (std::memcmp(memoryCandidate, memoryToCompare1, m_memSize) == 0);
      else
        isResult = (compareMemoryAsNumbers(memoryCandidate, memoryToCompare1, noOffset, false,
                                           false, m_memSize) == MemScanner::CompareResult::equal);
      break;
    }
    case ScanFiter::between:
    {
      MemScanner::CompareResult result1 = compareMemoryAsNumbers(memoryCandidate, memoryToCompare1,
                                                                 noOffset, false, false, m_memSize);
      MemScanner::CompareResult result2 = compareMemoryAsNumbers(memoryCandidate, memoryToCompare2,
                                                                 noOffset, false, false, m_memSize);
      isResult = ((result1 == MemScanner::CompareResult::bigger ||
                   result1 == MemScanner::CompareResult::equal) &&
                  (result2 == MemScanner::CompareResult::smaller ||
                   result2 == MemScanner::CompareResult::equal));
      break;
    }
    case ScanFiter::biggerThan:
    {
      isResult = (compareMemoryAsNumbers(memoryCandidate, memoryToCompare1, noOffset, false, false,
                                         m_memSize) == MemScanner::CompareResult::bigger);
      break;
    }
    case ScanFiter::smallerThan:
    {
      isResult = (compareMemoryAsNumbers(memoryCandidate, memoryToCompare1, noOffset, false, false,
                                         m_memSize) == MemScanner::CompareResult::smaller);
      break;
    }
    }

    if (isResult)
    {
      u32 consoleOffset = 0;
      if (i >= Common::MEM1_SIZE)
        consoleOffset = i + (Common::MEM2_START - Common::MEM1_END);
      else
        consoleOffset = i;
      m_resultsConsoleAddr.push_back(Common::offsetToDolphinAddr(consoleOffset));
    }
  }
  delete[] noOffset;
  m_resultCount = m_resultsConsoleAddr.size();
  m_scanStarted = true;
  return Common::MemOperationReturnCode::OK;
}

Common::MemOperationReturnCode MemScanner::nextScan(const MemScanner::ScanFiter filter,
                                                    const std::string& searchTerm1,
                                                    const std::string& searchTerm2)
{
  u32 ramSize = 0;
  char* newerRAMCache = nullptr;
  if (DolphinComm::DolphinAccessor::isMEM2Present())
  {
    ramSize = Common::MEM1_SIZE + Common::MEM2_SIZE;
    newerRAMCache = new char[ramSize - 1];
    if (!DolphinComm::DolphinAccessor::readFromRAM(Common::dolphinAddrToOffset(Common::MEM2_START),
                                                   newerRAMCache + Common::MEM1_SIZE,
                                                   Common::MEM2_SIZE - 1, false))
    {
      delete[] m_scanRAMCache;
      delete[] newerRAMCache;
      return Common::MemOperationReturnCode::operationFailed;
    }
  }
  else
  {
    ramSize = Common::MEM1_SIZE;
    newerRAMCache = new char[ramSize - 1];
  }

  if (!DolphinComm::DolphinAccessor::readFromRAM(Common::dolphinAddrToOffset(Common::MEM1_START),
                                                 newerRAMCache, Common::MEM1_SIZE - 1, false))
  {
    delete[] m_scanRAMCache;
    delete[] newerRAMCache;
    return Common::MemOperationReturnCode::operationFailed;
  }

  Common::MemOperationReturnCode scanReturn = Common::MemOperationReturnCode::OK;
  size_t termActualLength = 0;
  size_t termMaxLength = 0;
  if (m_memType == Common::MemType::type_string)
  {
    // This is just to have the string formatted with the appropriate length, byte arrays don't need
    // this because they get copied byte per byte
    termMaxLength = searchTerm1.length();
  }
  else
  {
    // Have no restriction on the length for the rest
    termMaxLength = ramSize;
  }
  char* memoryToCompare1 = nullptr;
  if (filter != ScanFiter::increased && filter != ScanFiter::decreased &&
      filter != ScanFiter::changed && filter != ScanFiter::unchanged)
  {
    memoryToCompare1 = Common::formatStringToMemory(scanReturn, termActualLength, searchTerm1,
                                                    m_memBase, m_memType, termMaxLength);
    if (scanReturn != Common::MemOperationReturnCode::OK)
      return scanReturn;
  }

  char* memoryToCompare2 = nullptr;
  if (filter == ScanFiter::between)
  {
    memoryToCompare2 = Common::formatStringToMemory(scanReturn, termActualLength, searchTerm2,
                                                    m_memBase, m_memType, ramSize);
    if (scanReturn != Common::MemOperationReturnCode::OK)
      return scanReturn;
  }

  bool withBSwap = Common::shouldBeBSwappedForType(m_memType);

  m_memSize = Common::getSizeForType(m_memType, termActualLength);

  char* noOffset = new char[m_memSize];
  std::memset(noOffset, 0, m_memSize);

  std::vector<u32> newerResults = std::vector<u32>();

  if (m_wasUnknownInitialValue)
  {
    m_wasUnknownInitialValue = false;
    for (u32 i = 0; i < (ramSize - m_memSize); ++i)
    {
      u32 consoleOffset = 0;
      if (i >= Common::MEM1_SIZE)
        consoleOffset = i + (Common::MEM2_START - Common::MEM1_END);
      else
        consoleOffset = i;
      if (isHitNextScan(filter, memoryToCompare1, memoryToCompare2, noOffset, newerRAMCache,
                        m_memSize, i))
      {
        newerResults.push_back(Common::offsetToDolphinAddr(consoleOffset));
      }
    }
  }
  else
  {
    for (auto i : m_resultsConsoleAddr)
    {
      u32 ramIndex = 0;
      if (Common::dolphinAddrToOffset(i) >= Common::MEM1_SIZE)
        ramIndex = Common::dolphinAddrToOffset(i) - (Common::MEM2_START - Common::MEM1_END);
      else
        ramIndex = Common::dolphinAddrToOffset(i);
      if (isHitNextScan(filter, memoryToCompare1, memoryToCompare2, noOffset, newerRAMCache,
                        m_memSize, ramIndex))
      {
        newerResults.push_back(i);
      }
    }
  }

  delete[] noOffset;
  m_resultsConsoleAddr.clear();
  std::swap(m_resultsConsoleAddr, newerResults);
  delete[] m_scanRAMCache;
  m_scanRAMCache = newerRAMCache;
  m_resultCount = m_resultsConsoleAddr.size();
  return Common::MemOperationReturnCode::OK;
}

void MemScanner::reset()
{
  m_resultsConsoleAddr.clear();
  m_wasUnknownInitialValue = false;
  delete[] m_scanRAMCache;
  m_scanRAMCache = nullptr;
  m_resultCount = 0;
  m_scanStarted = false;
}

inline bool MemScanner::isHitNextScan(const MemScanner::ScanFiter filter,
                                      const char* memoryToCompare1, const char* memoryToCompare2,
                                      const char* noOffset, const char* newerRAMCache,
                                      const size_t realSize, const u32 consoleOffset) const
{
  char* olderMemory = &m_scanRAMCache[consoleOffset];
  const char* newerMemory = &newerRAMCache[consoleOffset];

  switch (filter)
  {
  case ScanFiter::exact:
  {
    if (m_memType == Common::MemType::type_string || m_memType == Common::MemType::type_byteArray)
      return (std::memcmp(newerMemory, memoryToCompare1, realSize) == 0);
    else
      return (compareMemoryAsNumbers(newerMemory, memoryToCompare1, noOffset, false, false,
                                     realSize) == MemScanner::CompareResult::equal);
    break;
  }
  case ScanFiter::between:
  {
    MemScanner::CompareResult result1 =
        compareMemoryAsNumbers(newerMemory, memoryToCompare1, noOffset, false, false, realSize);
    MemScanner::CompareResult result2 =
        compareMemoryAsNumbers(newerMemory, memoryToCompare2, noOffset, false, false, realSize);
    return ((result1 == MemScanner::CompareResult::bigger ||
             result1 == MemScanner::CompareResult::equal) &&
            (result2 == MemScanner::CompareResult::smaller ||
             result2 == MemScanner::CompareResult::equal));
    break;
  }
  case ScanFiter::biggerThan:
  {
    return (compareMemoryAsNumbers(newerMemory, memoryToCompare1, noOffset, false, false,
                                   realSize) == MemScanner::CompareResult::bigger);
    break;
  }
  case ScanFiter::smallerThan:
  {
    return (compareMemoryAsNumbers(newerMemory, memoryToCompare1, noOffset, false, false,
                                   realSize) == MemScanner::CompareResult::smaller);
    break;
  }
  case ScanFiter::increasedBy:
  {
    return (compareMemoryAsNumbers(newerMemory, olderMemory, memoryToCompare1, false, true,
                                   realSize) == MemScanner::CompareResult::equal);
    break;
  }
  case ScanFiter::decreasedBy:
  {
    return (compareMemoryAsNumbers(newerMemory, olderMemory, memoryToCompare1, true, true,
                                   realSize) == MemScanner::CompareResult::equal);
    break;
  }
  case ScanFiter::increased:
  {
    return (compareMemoryAsNumbers(newerMemory, olderMemory, noOffset, false, true, realSize) ==
            MemScanner::CompareResult::bigger);
    break;
  }
  case ScanFiter::decreased:
  {
    return (compareMemoryAsNumbers(newerMemory, olderMemory, noOffset, false, true, realSize) ==
            MemScanner::CompareResult::smaller);
    break;
  }
  case ScanFiter::changed:
  {
    MemScanner::CompareResult result =
        compareMemoryAsNumbers(newerMemory, olderMemory, noOffset, false, true, realSize);
    return (result == MemScanner::CompareResult::bigger ||
            result == MemScanner::CompareResult::smaller);
    break;
  }
  case ScanFiter::unchanged:
  {
    return (compareMemoryAsNumbers(newerMemory, olderMemory, noOffset, false, true, realSize) ==
            MemScanner::CompareResult::equal);
    break;
  }
  default:
  {
    return false;
  }
  }
}

inline MemScanner::CompareResult
MemScanner::compareMemoryAsNumbers(const char* first, const char* second, const char* offset,
                                   bool offsetInvert, bool bswapSecond, size_t length) const
{
  switch (m_memType)
  {
  case Common::MemType::type_byte:
  {
    if (m_memIsSigned)
    {
      return compareMemoryAsNumbersWithType<s8>(first, second, offset, offsetInvert, bswapSecond);
    }
    return compareMemoryAsNumbersWithType<u8>(first, second, offset, offsetInvert, bswapSecond);
    break;
  }
  case Common::MemType::type_halfword:
  {
    if (m_memIsSigned)
    {
      return compareMemoryAsNumbersWithType<s16>(first, second, offset, offsetInvert, bswapSecond);
    }
    return compareMemoryAsNumbersWithType<u16>(first, second, offset, offsetInvert, bswapSecond);
    break;
  }
  case Common::MemType::type_word:
  {
    if (m_memIsSigned)
    {
      return compareMemoryAsNumbersWithType<s32>(first, second, offset, offsetInvert, bswapSecond);
    }
    return compareMemoryAsNumbersWithType<u32>(first, second, offset, offsetInvert, bswapSecond);
    break;
  }
  case Common::MemType::type_float:
  {
    return compareMemoryAsNumbersWithType<float>(first, second, offset, offsetInvert, bswapSecond);
    break;
  }
  case Common::MemType::type_double:
  {
    return compareMemoryAsNumbersWithType<double>(first, second, offset, offsetInvert, bswapSecond);
    break;
  }
  default:
  {
    return MemScanner::CompareResult::nan;
  }
  }
}

void MemScanner::setType(const Common::MemType type)
{
  m_memType = type;
}

void MemScanner::setBase(const Common::MemBase base)
{
  m_memBase = base;
}

void MemScanner::setIsSigned(const bool isSigned)
{
  m_memIsSigned = isSigned;
}

int MemScanner::getTermsNumForFilter(const MemScanner::ScanFiter filter) const
{
  if (filter == MemScanner::ScanFiter::between)
    return 2;
  else if (filter == MemScanner::ScanFiter::exact || filter == MemScanner::ScanFiter::increasedBy ||
           filter == MemScanner::ScanFiter::decreasedBy ||
           filter == MemScanner::ScanFiter::biggerThan ||
           filter == MemScanner::ScanFiter::smallerThan)
    return 1;
  return 0;
}

bool MemScanner::typeSupportsAdditionalOptions(const Common::MemType type) const
{
  return (type == Common::MemType::type_byte || type == Common::MemType::type_halfword ||
          type == Common::MemType::type_word);
}

std::vector<u32> MemScanner::getResultsConsoleAddr() const
{
  return m_resultsConsoleAddr;
}

std::string MemScanner::getFormattedScannedValueAt(const int index) const
{
  u32 offset = Common::dolphinAddrToOffset(m_resultsConsoleAddr.at(index));
  u32 ramIndex = 0;
  if (offset >= Common::MEM1_SIZE)
    ramIndex = offset - (Common::MEM2_START - Common::MEM1_END);
  else
    ramIndex = offset;
  return Common::formatMemoryToString(&m_scanRAMCache[ramIndex], m_memType, m_memSize, m_memBase,
                                      !m_memIsSigned, Common::shouldBeBSwappedForType(m_memType));
}

Common::MemOperationReturnCode MemScanner::updateCurrentRAMCache()
{
  return DolphinComm::DolphinAccessor::updateRAMCache();
}

std::string MemScanner::getFormattedCurrentValueAt(const int index) const
{
  if (DolphinComm::DolphinAccessor::isValidConsoleAddress(m_resultsConsoleAddr.at(index)))
  {
    u32 offset = Common::dolphinAddrToOffset(m_resultsConsoleAddr.at(index));
    u32 ramIndex = 0;
    if (offset >= Common::MEM1_SIZE)
      ramIndex = offset - (Common::MEM2_START - Common::MEM1_END);
    else
      ramIndex = offset;
    return DolphinComm::DolphinAccessor::getFormattedValueFromCache(ramIndex, m_memType, m_memSize,
                                                                    m_memBase, !m_memIsSigned);
  }
  return "";
}

size_t MemScanner::getResultCount() const
{
  return m_resultCount;
}

bool MemScanner::hasScanStarted() const
{
  return m_scanStarted;
}

Common::MemType MemScanner::getType() const
{
  return m_memType;
}

Common::MemBase MemScanner::getBase() const
{
  return m_memBase;
}

size_t MemScanner::getLength() const
{
  return m_memSize;
}

bool MemScanner::getIsUnsigned() const
{
  return !m_memIsSigned;
}
