#ifndef H_rts_segment_DictionarySegment
#define H_rts_segment_DictionarySegment
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
#include <string>
#include <vector>
//---------------------------------------------------------------------------
class DatabaseBuilder;
//---------------------------------------------------------------------------
/// A dictionary mapping strings to ids and backwards
class DictionarySegment : public Segment
{
   private:
   /// The start of the raw string table
   unsigned tableStart;
   /// The next id after the existing ones
   unsigned nextId;
   /// The start of the mapping table (id->page)
   unsigned mappingStart;
   /// The root of the index b-tree
   unsigned indexRoot;

   /// Refresh segment info stored in the partition
   void refreshInfo();
   /// Lookup an id for a given string on a certain page in the raw string table
   bool lookupOnPage(unsigned pageNo,const std::string& text,unsigned hash,unsigned& id);

   /// Load the raw strings (must be in id order)
   void loadStrings(void* reader);
   /// Load the string mappings (must be in id order)
   void loadStringMappings(void* reader);
   /// Write the leaf nodes of the string index
   void writeStringLeaves(void* reader,void* boundaries);
   /// Write inner nodes
   void writeStringInner(const void* data,void* boundaries);
   /// Write the string index
   void loadStringHashes(void* reader);

   friend class DatabaseBuilder;

   public:
   /// Constructor
   DictionarySegment(DatabasePartition& partition);

   /// Get the type
   Type getType() const;

   /// Lookup an id for a given string
   bool lookup(const std::string& text,unsigned& id);
   /// Lookup a string for a given id
   bool lookupById(unsigned id,const char*& start,const char*& stop);
   /// Lookup a string for a given id
   std::string mapId(unsigned id);

   /// Load new strings into the dictionary
   void appendStrings(const std::vector<std::string>& strings);
};
//---------------------------------------------------------------------------
#endif
