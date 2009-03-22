#include "rts/database/DatabaseBuilder.hpp"
#include "infra/osdep/MemoryMappedFile.hpp"
#include "infra/util/Hash.hpp"
#include "rts/buffer/BufferReference.hpp"
#include "rts/database/DatabasePartition.hpp"
#include "rts/segment/AggregatedFactsSegment.hpp"
#include "rts/segment/DictionarySegment.hpp"
#include "rts/segment/ExactStatisticsSegment.hpp"
#include "rts/segment/FactsSegment.hpp"
#include "rts/segment/FullyAggregatedFactsSegment.hpp"
#include "rts/segment/Segment.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
//---------------------------------------------------------------------------
// RDF-3X
// (c) 2008 Thomas Neumann. Web site: http://www.mpi-inf.mpg.de/~neumann/rdf3x
//
// This work is licensed under the Creative Commons
// Attribution-Noncommercial-Share Alike 3.0 Unported License. To view a copy
// of this license, visit http://creativecommons.org/licenses/by-nc-sa/3.0/
// or send a letter to Creative Commons, 171 Second Street, Suite 300,
// San Francisco, California, 94105, USA.
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
const unsigned pageSize = BufferReference::pageSize;
//---------------------------------------------------------------------------
bool DatabaseBuilder::PutbackReader::next(unsigned& subject,unsigned& predicate,unsigned& object)
   // Get the next triple
{
   if (hasPutback) {
      subject=this->subject; predicate=this->predicate; object=this->object;
      hasPutback=false;
      return true;
   } else return reader.next(subject,predicate,object);
}
//---------------------------------------------------------------------------
void DatabaseBuilder::PutbackReader::putBack(unsigned subject,unsigned predicate,unsigned object)
   // Put a triple back
{
   this->subject=subject; this->predicate=predicate; this->object=object;
   hasPutback=true;
}
//---------------------------------------------------------------------------
DatabaseBuilder::FactsReader::FactsReader()
   // Constructor
{
}
//---------------------------------------------------------------------------
DatabaseBuilder::FactsReader::~FactsReader()
   // Destructor
{
}
//---------------------------------------------------------------------------
DatabaseBuilder::StringsReader::StringsReader()
   // Constructor
{
}
//---------------------------------------------------------------------------
DatabaseBuilder::StringsReader::~StringsReader()
   // Destructor
{
}
//---------------------------------------------------------------------------
DatabaseBuilder::StringInfoReader::StringInfoReader()
   // Constructor
{
}
//---------------------------------------------------------------------------
DatabaseBuilder::StringInfoReader::~StringInfoReader()
   // Destructor
{
}
//---------------------------------------------------------------------------
DatabaseBuilder::PageChainer::PageChainer(unsigned ofs)
   : ofs(ofs),firstPage(0),pages(0)
   // Constructor
{
}
//---------------------------------------------------------------------------
DatabaseBuilder::PageChainer::~PageChainer()
   // Destructor
{
}
//---------------------------------------------------------------------------
void DatabaseBuilder::PageChainer::store(Segment* seg,const void* pageData)
   // Store a page
{
   memcpy(nextPage(seg),pageData,BufferReference::pageSize);
   memset(currentPage.getPage(),0,8);
   Segment::writeUint32(static_cast<unsigned char*>(currentPage.getPage())+ofs,0);
}
//---------------------------------------------------------------------------
void* DatabaseBuilder::PageChainer::nextPage(Segment* seg)
   // Store a page
{
   currentPage.swap(lastPage);
   seg->allocPage(currentPage);
   if (!!lastPage) {
      // Update the link
      Segment::writeUint32(static_cast<unsigned char*>(lastPage.getPage())+ofs,currentPage.getPageNo());
      lastPage.unfixWithoutRecovery();
   } else {
      firstPage=currentPage.getPageNo();
   }

   Segment::writeUint32(static_cast<unsigned char*>(currentPage.getPage())+ofs,0);
   ++pages;

   return currentPage.getPage();
}
//---------------------------------------------------------------------------
void DatabaseBuilder::PageChainer::finish()
   // Finish chaining
{
   currentPage.unfixWithoutRecovery();
}
//---------------------------------------------------------------------------
DatabaseBuilder::DatabaseBuilder(const char* fileName)
   : dbFile(fileName)
   // Constructor
{
   // Create the database
   if (!out.create(fileName)) {
      cerr << "unable to create " << fileName << endl;
      throw;
   }
}
//---------------------------------------------------------------------------
DatabaseBuilder::~DatabaseBuilder()
   // Destructor
{
}
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// A source for the facts segment
class FactsSegmentSource : public FactsSegment::Source
{
   private:
   /// The real source
   DatabaseBuilder::FactsReader& reader;

   public:
   /// Constructor
   FactsSegmentSource(DatabaseBuilder::FactsReader& reader) : reader(reader) {}

   /// Get the next entry
   bool next(unsigned& value1,unsigned& value2,unsigned& value3) { return reader.next(value1,value2,value3); }
   /// Mark as duplicate
   void markAsDuplicate() {}
};
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
void DatabaseBuilder::loadFacts(unsigned order,FactsReader& reader)
   // Loads the facts in a given order
{
   // Load the full facts first
   FactsSegment* fullFacts=new FactsSegment(out.getFirstPartition());
   out.getFirstPartition().addSegment(fullFacts,DatabasePartition::Tag_SPO+order);
   reader.reset();
   {
      FactsSegmentSource source(reader);
      fullFacts->loadFullFacts(source);
   }

   // Load the aggregated facts
   AggregatedFactsSegment* aggregatedFacts=new AggregatedFactsSegment(out.getFirstPartition());
   out.getFirstPartition().addSegment(aggregatedFacts,DatabasePartition::Tag_SP+order);
   reader.reset();
   aggregatedFacts->loadAggregatedFacts(&reader);

   // Load the fully aggregated facts
   FullyAggregatedFactsSegment* fullyAggregatedFacts=0;
   if ((order&1)==0) {
      fullyAggregatedFacts=new FullyAggregatedFactsSegment(out.getFirstPartition());
      out.getFirstPartition().addSegment(fullyAggregatedFacts,DatabasePartition::Tag_S+(order/2));
      reader.reset();
      fullyAggregatedFacts->loadFullyAggregatedFacts(&reader);
   }

   // Compute the tuple statistics
   reader.reset();
   unsigned subject,predicate,object;
   unsigned groups1,groups2,cardinality;
   if (!reader.next(subject,predicate,object)) {
      groups1=0;
      groups2=0;
      cardinality=0;
   } else {
      groups1=1;
      groups2=1;
      cardinality=1;
      unsigned nextSubject,nextPredicate,nextObject;
      while (reader.next(nextSubject,nextPredicate,nextObject)) {
         if (nextSubject!=subject) {
            groups1++;
            groups2++;
            cardinality++;
            subject=nextSubject; predicate=nextPredicate; object=nextObject;
         } else if (nextPredicate!=predicate) {
            groups2++;
            cardinality++;
            predicate=nextPredicate; object=nextObject;
         } else if (nextObject!=object) {
            cardinality++;
            object=nextObject;
         }
      }
   }
   fullFacts->loadCounts(groups1,groups2,cardinality);
   aggregatedFacts->loadCounts(groups1,groups2);
   if (fullyAggregatedFacts)
      fullyAggregatedFacts->loadCounts(groups1);
}
//---------------------------------------------------------------------------
void DatabaseBuilder::loadStrings(StringsReader& reader)
   // Load the raw strings (must be in id order)
{
   DictionarySegment* seg=new DictionarySegment(out.getFirstPartition());
   out.getFirstPartition().addSegment(seg,DatabasePartition::Tag_Dictionary);
   seg->loadStrings(&reader);
}
//---------------------------------------------------------------------------
void DatabaseBuilder::loadStringMappings(DatabaseBuilder::StringInfoReader& reader)
   // Load the string mappings (must be in id order)
{
   DictionarySegment* seg=out.getFirstPartition().lookupSegment<DictionarySegment>(DatabasePartition::Tag_Dictionary);
   seg->loadStringMappings(&reader);
}
//---------------------------------------------------------------------------
void DatabaseBuilder::loadStringHashes(StringInfoReader& reader)
   // Write the string index
{
   DictionarySegment* seg=out.getFirstPartition().lookupSegment<DictionarySegment>(DatabasePartition::Tag_Dictionary);
   seg->loadStringHashes(&reader);
}
//---------------------------------------------------------------------------
static void buildCountMap(Database& db,const char* fileName)
   // Build a map with aggregated counts
{
   // Prepare the output file
   ofstream out(fileName,ios::out|ios::binary|ios::trunc);
   if (!out.is_open()) {
      cerr << "unable to write " << fileName << endl;
      throw;
   }

   // Scan all aggregated indices at once
   FullyAggregatedFactsSegment::Scan scanS,scanO,scanP;
   bool doneS=!scanS.first(db.getFullyAggregatedFacts(Database::Order_Subject_Predicate_Object));
   bool doneP=!scanP.first(db.getFullyAggregatedFacts(Database::Order_Predicate_Subject_Object));
   bool doneO=!scanO.first(db.getFullyAggregatedFacts(Database::Order_Object_Subject_Predicate));
   for (unsigned id=0;(!doneS)||(!doneP)||(!doneO);++id) {
      unsigned counts[3];
      if (doneS||(scanS.getValue1()>id)) {
         counts[0]=0;
      } else {
         counts[0]=scanS.getCount();
         doneS=!scanS.next();
      }
      if (doneP||(scanP.getValue1()>id)) {
         counts[1]=0;
      } else {
         counts[1]=scanP.getCount();
         doneP=!scanP.next();
      }
      if (doneO||(scanO.getValue1()>id)) {
         counts[2]=0;
      } else {
         counts[2]=scanO.getCount();
         doneO=!scanO.next();
      }
      out.write(reinterpret_cast<char*>(counts),3*sizeof(unsigned));
   }
   out.flush();
}
//---------------------------------------------------------------------------
void DatabaseBuilder::computeExactStatistics(const char* tmpFile)
   // Compute exact statistics (after loading)
{
   // Build aggregated count map
   buildCountMap(out,tmpFile);
   MemoryMappedFile countMap;
   if (!countMap.open(tmpFile)) {
      cout << "Unable to open " << tmpFile << endl;
      throw;
   }

   // And build the statistics
   ExactStatisticsSegment* seg=new ExactStatisticsSegment(out.getFirstPartition());
   out.getFirstPartition().addSegment(seg,DatabasePartition::Tag_ExactStatistics);
   seg->computeExactStatistics(countMap);

   // Remove the map
   countMap.close();
   remove(tmpFile);
}
//---------------------------------------------------------------------------
