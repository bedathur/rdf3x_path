#ifndef H_rts_segment_FullyAggregatedFactsSegment
#define H_rts_segment_FullyAggregatedFactsSegment
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
#include "rts/segment/Segment.hpp"
#include "rts/buffer/BufferReference.hpp"
//---------------------------------------------------------------------------
/// Fully aggregated facts, i.e. counts for single values
class FullyAggregatedFactsSegment : public Segment
{
   public:
   /// A source for updates
   class Source {
      public:
      /// Destructor
      virtual ~Source();

      /// Get the next triples
      virtual bool next(unsigned& value1,unsigned& count) = 0;
      /// Mark the last triple as duplicate
      virtual void markAsDuplicate() = 0;
   };

   private:
   /// The index
   class Index;

   /// The start of the raw facts table
   unsigned tableStart;
   /// The root of the index b-tree
   unsigned indexRoot;
   /// Statistics
   unsigned pages,groups1;

   /// Refresh segment info stored in the partition
   void refreshInfo();

   /// Load the triples into the database
   void loadFullyAggregatedFacts(Source& reader);
   /// Load count statistics
   void loadCounts(unsigned groups1);

   FullyAggregatedFactsSegment(const FullyAggregatedFactsSegment&);
   void operator=(const FullyAggregatedFactsSegment&);

   friend class DatabaseBuilder;

   public:
   /// Constructor
   explicit FullyAggregatedFactsSegment(DatabasePartition& partition);

   /// Get the type
   Type getType() const;

   /// Get the number of pages in the segment
   unsigned getPages() const { return pages; }
   /// Get the number of level 1 groups
   unsigned getLevel1Groups() const { return groups1; }

   /// Update the segment
   void update(Source& source);

   /// A scan over the facts segment
   class Scan {
      public:
      /// Hints for skipping through the scan
      class Hint {
         public:
         /// Constructor
         Hint();
         /// Destructor
         virtual ~Hint();

         /// The hint
         virtual void next(unsigned& value1) = 0;
      };

      private:
      /// The maximum number of entries per page
      static const unsigned maxCount = BufferReference::pageSize;
      /// A (aggregated) triple
      struct Triple {
         unsigned value1,count;
      };

      /// The current page
      BufferReference current;
      /// The segment
      FullyAggregatedFactsSegment* seg;
      /// The position on the current page
      const Triple* pos,*posLimit;
      /// The decompressed triples
      Triple triples[maxCount];
      /// The scan hint
      Hint* hint;

      Scan(const Scan&);
      void operator=(const Scan&);

      /// Perform a binary search
      bool find(unsigned value1);
      /// Read the next page
      bool readNextPage();

      public:
      /// Constructor
      explicit Scan(Hint* hint=0);
      /// Destructor
      ~Scan();

      /// Start a new scan over the whole segment and reads the first entry
      bool first(FullyAggregatedFactsSegment& segment);
      /// Start a new scan starting from the first entry >= the start condition and reads the first entry
      bool first(FullyAggregatedFactsSegment& segment,unsigned start1);

      /// Read the next entry
      bool next() { if ((++pos)>=posLimit) return readNextPage(); else return true; }
      /// Get the first value
      unsigned getValue1() const { return (*pos).value1; }
      /// Get the count
      unsigned getCount() const { return (*pos).count; }

      /// Close the scan
      void close();
   };
   friend class Scan;
};
//---------------------------------------------------------------------------
#endif
