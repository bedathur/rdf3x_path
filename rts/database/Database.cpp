#include "rts/database/Database.hpp"
#include "rts/buffer/BufferManager.hpp"
#include "rts/segment/AggregatedFactsSegment.hpp"
#include "rts/segment/DictionarySegment.hpp"
#include "rts/segment/FactsSegment.hpp"
#include "rts/segment/FullyAggregatedFactsSegment.hpp"
#include "rts/segment/StatisticsSegment.hpp"
#include "rts/segment/PathStatisticsSegment.hpp"
//---------------------------------------------------------------------------
Database::Database()
   : bufferManager(0),dictionary(0)
   // Constructor
{
   for (unsigned index=0;index<6;index++) {
      facts[index]=0;
      aggregatedFacts[index]=0;
      statistics[index]=0;
   }
   for (unsigned index=0;index<3;index++)
      fullyAggregatedFacts[index]=0;
   for (unsigned index=0;index<2;index++)
      pathStatistics[index]=0;
}
//---------------------------------------------------------------------------
Database::~Database()
   // Destructor
{
   close();
}
//---------------------------------------------------------------------------
static unsigned readUint32(const unsigned char* data) { return (data[0]<<24)|(data[1]<<16)|(data[2]<<8)|data[3]; }
//---------------------------------------------------------------------------
bool Database::open(const char* fileName)
   // Open a database
{
   close();

   // Try to open the database
   bufferManager=new BufferManager();
   if (bufferManager->open(fileName)&&(bufferManager->getPageCount()>0)) {
      // Read the directory page
      BufferReference directory;
      bufferManager->readExclusive(directory,0);
      const unsigned char* page=static_cast<const unsigned char*>(directory.getPage());

      // Check the header
      if ((readUint32(page)==(('R'<<24)|('D'<<16)|('F'<<8)))&&(readUint32(page+4)==1)) {
         // Read the page infos
         unsigned factStarts[6],factIndices[6],aggregatedFactStarts[6],aggregatedFactIndices[6];
         unsigned fullyAggregatedFactStarts[3],fullyAggregatedFactIndices[3];
         unsigned pageCounts[6],aggregatedPageCounts[6],groups1[6],groups2[6],cardinalities[6];
         for (unsigned index=0;index<6;index++) {
            factStarts[index]=readUint32(page+8+index*36);
            factIndices[index]=readUint32(page+8+index*36+4);
            aggregatedFactStarts[index]=readUint32(page+8+index*36+8);
            aggregatedFactIndices[index]=readUint32(page+8+index*36+12);
            pageCounts[index]=readUint32(page+8+index*36+16);
            aggregatedPageCounts[index]=readUint32(page+8+index*36+20);
            groups1[index]=readUint32(page+8+index*36+24);
            groups2[index]=readUint32(page+8+index*36+28);
            cardinalities[index]=readUint32(page+8+index*36+32);
         }
         for (unsigned index=0;index<3;index++) {
            fullyAggregatedFactStarts[index]=readUint32(page+224+index*8);
            fullyAggregatedFactIndices[index]=readUint32(page+224+index*8+4);
         }
         unsigned stringStart=readUint32(page+248);
         unsigned stringMapping=readUint32(page+252);
         unsigned stringIndex=readUint32(page+256);
         unsigned statisticsPages[6];
         for (unsigned index=0;index<6;index++)
            statisticsPages[index]=readUint32(page+260+4*index);
         unsigned pathStatisticsPages[2];
         for (unsigned index=0;index<2;index++)
            pathStatisticsPages[index]=readUint32(page+284+4*index);

         // Construct the segments
         for (unsigned index=0;index<6;index++) {
            facts[index]=new FactsSegment(*bufferManager,factStarts[index],factIndices[index],pageCounts[index],groups1[index],groups2[index],cardinalities[index]);
            aggregatedFacts[index]=new AggregatedFactsSegment(*bufferManager,aggregatedFactStarts[index],aggregatedFactIndices[index],aggregatedPageCounts[index],groups1[index],groups2[index]);
            statistics[index]=new StatisticsSegment(*bufferManager,statisticsPages[index]);
         }
         for (unsigned index=0;index<3;index++)
            fullyAggregatedFacts[index]=new FullyAggregatedFactsSegment(*bufferManager,fullyAggregatedFactStarts[index],fullyAggregatedFactIndices[index],fullyAggregatedFactIndices[index]-fullyAggregatedFactStarts[index],groups1[index*2]);
         for (unsigned index=0;index<2;index++)
            pathStatistics[index]=new PathStatisticsSegment(*bufferManager,pathStatisticsPages[index]);
         dictionary=new DictionarySegment(*bufferManager,stringStart,stringMapping,stringIndex);

         return true;
      }
   }

   // Failure, cleanup
   delete bufferManager;
   bufferManager=0;
   return false;
}
//---------------------------------------------------------------------------
void Database::close()
   // Close the current database
{
   for (unsigned index=0;index<6;index++) {
      delete facts[index];
      facts[index]=0;
      delete aggregatedFacts[index];
      aggregatedFacts[index]=0;
      delete statistics[index];
      statistics[index]=0;
   }
   for (unsigned index=0;index<3;index++) {
      delete fullyAggregatedFacts[index];
      fullyAggregatedFacts[index]=0;
   }
   for (unsigned index=0;index<2;index++) {
      delete pathStatistics[index];
      pathStatistics[index]=0;
   }
   delete dictionary;
   dictionary=0;
   delete bufferManager;
   bufferManager=0;
}
//---------------------------------------------------------------------------
FactsSegment& Database::getFacts(DataOrder order)
   // Get the facts
{
   return *(facts[order]);
}
//---------------------------------------------------------------------------
AggregatedFactsSegment& Database::getAggregatedFacts(DataOrder order)
   // Get the facts
{
   return *(aggregatedFacts[order]);
}
//---------------------------------------------------------------------------
FullyAggregatedFactsSegment& Database::getFullyAggregatedFacts(DataOrder order)
   // Get fully aggregated fcats
{
   return *(fullyAggregatedFacts[order/2]);
}
//---------------------------------------------------------------------------
StatisticsSegment& Database::getStatistics(DataOrder order)
   // Get fact statistics
{
   return *(statistics[order]);
}
//---------------------------------------------------------------------------
PathStatisticsSegment& Database::getPathStatistics(bool stars)
   // Get path statistics
{
   return *(pathStatistics[stars]);
}
//---------------------------------------------------------------------------
DictionarySegment& Database::getDictionary()
   // Get the dictionary
{
   return *dictionary;
}
//---------------------------------------------------------------------------
