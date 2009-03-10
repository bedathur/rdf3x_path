#include "../rdf3xload/TurtleParser.hpp"
#include "rts/database/Database.hpp"
#include "rts/runtime/BulkOperation.hpp"
#include "infra/osdep/Event.hpp"
#include "infra/osdep/Mutex.hpp"
#include "infra/osdep/Timestamp.hpp"
#include "infra/osdep/Thread.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <cstring>
#include <cstdlib>
//---------------------------------------------------------------------------
// RDF-3X
// (c) 2009 Thomas Neumann. Web site: http://www.mpi-inf.mpg.de/~neumann/rdf3x
//
// This work is licensed under the Creative Commons
// Attribution-Noncommercial-Share Alike 3.0 Unported License. To view a copy
// of this license, visit http://creativecommons.org/licenses/by-nc-sa/3.0/
// or send a letter to Creative Commons, 171 Second Street, Suite 300,
// San Francisco, California, 94105, USA.
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace {
//---------------------------------------------------------------------------
/// A work description
struct WorkDescription {
   /// The synchronizing mutex
   Mutex mutex;
   /// Notification
   Event event;
   /// The chunks
   vector<string> chunkFiles;
   /// The current work position
   unsigned workPos;
   /// Active workers
   unsigned activeWorkers;
   /// The differential index
   DifferentialIndex* db;
   /// Total number of processed triples
   unsigned tripleCount;

   /// Constructor
   WorkDescription() : workPos(0),activeWorkers(0),db(0),tripleCount(0) {}
};
//---------------------------------------------------------------------------
static void worker(void* data)
   // A worker thread
{
   WorkDescription& work=*static_cast<WorkDescription*>(data);

   unsigned processed = 0;
   while (true) {
      // Check for new work
      work.mutex.lock();
      work.tripleCount+=processed;
      processed=0;
      if (work.workPos>=work.chunkFiles.size()) {
         work.activeWorkers--;
         work.event.notify(work.mutex);
         work.mutex.unlock();
         break;
      }
      string chunkFile=work.chunkFiles[work.workPos++];
      work.mutex.unlock();

      // Process the file
      BulkOperation bulk(*work.db);
      ifstream in(chunkFile.c_str());
      TurtleParser parser(in);
      string subject,predicate,object;
      while (true) {
         if (!parser.parse(subject,predicate,object))
            break;
         bulk.insert(subject,predicate,object);
         processed++;
      }
      bulk.commit();
   }
}
//---------------------------------------------------------------------------
static void writeURI(ostream& out,const string& str)
   // Write a URI
{
   out << "<";
   for (string::const_iterator iter=str.begin(),limit=str.end();iter!=limit;++iter) {
      char c=*iter;
      switch (c) {
         case '\t': out << "\\t"; break;
         case '\n': out << "\\n"; break;
         case '\r': out << "\\r"; break;
         case '>': out << "\\>"; break;
         case '\\': out << "\\\\"; break;
         default: out << c; break;
      }
   }
   out << ">";
}
//---------------------------------------------------------------------------
static void writeLiteral(ostream& out,const string& str)
   // Write a literal
{
   out << "\"";
   for (string::const_iterator iter=str.begin(),limit=str.end();iter!=limit;++iter) {
      char c=*iter;
      switch (c) {
         case '\t': out << "\\t"; break;
         case '\n': out << "\\n"; break;
         case '\r': out << "\\r"; break;
         case '\"': out << "\\\""; break;
         case '\\': out << "\\\\"; break;
         default: out << c; break;
      }
   }
   out << "\"";
}
//---------------------------------------------------------------------------
static void dumpSubject(ostream& out,const string& str)
   // Write a subject entry
{
   writeURI(out,str);
}
//---------------------------------------------------------------------------
static void dumpPredicate(ostream& out,const string& str)
   // Write a predicate entry
{
   writeURI(out,str);
}
//---------------------------------------------------------------------------
static bool isBlankNode(const string& str)
   // Looks like a blank node?
{
   return (str.size()>2)&&(str[0]=='_')&&(str[1]==':');
}
//---------------------------------------------------------------------------
static bool isURI(const string& str)
   // Looks like a URI?
{
   if (str.size()<5)
      return false;
   unsigned limit=str.size()-5;
   if (limit>10) limit=10;
   for (unsigned index=0;index<limit;index++) {
      char c=str[index];
      if (c==' ') break;
      if (c==':')
         return (str[index+1]=='/')&&(str[index+2]=='/');
   }
   return false;
}
//---------------------------------------------------------------------------
static void dumpObject(ostream& out,const string& str)
   // Write an object entry
{
   // Blank node or URI?
   if (isBlankNode(str)||isURI(str)) {
      writeURI(out,str);
      return;
   }
   // No, a literal value
   writeLiteral(out,str);
}
//---------------------------------------------------------------------------
}
//---------------------------------------------------------------------------
int main(int argc,char* argv[])
{
   if (argc!=2) {
      cerr << "usage: " << argv[0] << " <input>" << endl;
      return 1;
   }

   // Try to open the input
   ifstream in(argv[1]);
   if (!in.is_open()) {
      cerr << "unable to open " << argv[1] << endl;
      return 1;
   }
   TurtleParser parser(in);

   // Build a small database first
   cerr << "Building initial database..." << endl;
   {
      static const unsigned initialSize = 100000; //00;
      ofstream out("updatetest.1.tmp");
      string subject,predicate,object;
      for (unsigned index=0;index<initialSize;index++) {
         if (!parser.parse(subject,predicate,object))
            break;
         dumpSubject(out,subject);
         out << " ";
         dumpPredicate(out,predicate);
         out << " ";
         dumpObject(out,object);
         out << "." << endl;
      }
   }
   if (system("./bin/rdf3xload updatetest.2.tmp updatetest.1.tmp")!=0) {
      remove("updatetest.1.tmp");
      cerr << "unable to execute ./bin/rdf3xload" << endl;
      return 1;
   }
   remove("updatetest.1.tmp");

   // Prepare some triple chunks
   vector<string> chunkFiles;
   static const unsigned chunkSize = 10000;
   static const unsigned chunkCount = 10;
   for (unsigned index=0;index<chunkCount;index++) {
      stringstream sname; sname << "updatetest.chunk" << index << ".tmp";
      string name=sname.str();
      ofstream out(name.c_str());
      string subject,predicate,object;
      for (unsigned index2=0;index2<chunkSize;index2++) {
         if (!parser.parse(subject,predicate,object))
            break;
         dumpSubject(out,subject);
         out << " ";
         dumpPredicate(out,predicate);
         out << " ";
         dumpObject(out,object);
         out << "." << endl;
      }
      chunkFiles.push_back(name);
   }

   // Open the database again
   Database db;
   if (!db.open("updatetest.2.tmp")) {
      cerr << "unable to open updatetest.2.tmp" << endl;
      return 1;
   }
   DifferentialIndex diff(db);
   WorkDescription work;
   work.chunkFiles=chunkFiles;
   work.db=&diff;

   // Apply some updates
   cerr << "Applying updates..." << endl;
   Timestamp t1;
   if (work.chunkFiles.size()>10)
      work.chunkFiles.resize(10);
   work.mutex.lock();
   static const unsigned threadCount = 2;
   for (unsigned index=0;index<threadCount;index++) {
      work.activeWorkers++;
      Thread::start(worker,&work);
   }
   while (work.activeWorkers)
      work.event.wait(work.mutex);
   work.mutex.unlock();
   Timestamp t2;
   diff.sync();
   Timestamp t3;

   cerr << "Transaction time: " << (t2-t1) << endl;
   cerr << "Total time: " << (t3-t1) << endl;
   cerr << "Triples/s: " << (work.tripleCount*1000/(t3-t1)) << endl;

   remove("updatetest.2.tmp");
   for (vector<string>::const_iterator iter=chunkFiles.begin(),limit=chunkFiles.end();iter!=limit;++iter)
       remove((*iter).c_str());

   cerr << "Done." << endl;
}
//---------------------------------------------------------------------------
