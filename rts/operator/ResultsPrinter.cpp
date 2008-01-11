#include "rts/operator/ResultsPrinter.hpp"
#include "rts/database/Database.hpp"
#include "rts/runtime/Runtime.hpp"
#include "rts/segment/DictionarySegment.hpp"
#include <iostream>
#include <map>
//---------------------------------------------------------------------------
ResultsPrinter::ResultsPrinter(Database& db,Operator* input,const std::vector<Register*>& output,DuplicateHandling duplicateHandling,bool silent)
   : output(output),input(input),dictionary(db.getDictionary()),duplicateHandling(duplicateHandling),silent(silent)
   // Constructor
{
}
//---------------------------------------------------------------------------
ResultsPrinter::~ResultsPrinter()
   // Destructor
{
}
//---------------------------------------------------------------------------
unsigned ResultsPrinter::first()
   // Produce the first tuple
{
   // Empty input?
   unsigned count;
   if ((count=input->first())==0) {
      if (!silent)
         std::cout << "<empty result>" << std::endl;
      return true;
   }

   // Prepare the constants cache
   static const unsigned cacheSize = 65536;
   unsigned constantCache[cacheSize];
   const char* cacheStart[cacheSize],*cacheStop[cacheSize];
   for (unsigned index=0;index<cacheSize;index++)
      constantCache[index]=~0u;

   // Expand duplicates?
   if (duplicateHandling==ExpandDuplicates) {
      do {
         bool first=true;
         std::string s;
         for (std::vector<Register*>::const_iterator iter=output.begin(),limit=output.end();iter!=limit;++iter) {
            if (first) first=false; else if (!silent) s+=' ';
            if (~((*iter)->value)) {
               unsigned value=(*iter)->value,slot=value%cacheSize;
               if (constantCache[slot]!=value) {
                  constantCache[slot]=value;
                  dictionary.lookupById(value,cacheStart[slot],cacheStop[slot]);
               }
               if (!silent)
                  s+=std::string(cacheStart[slot],cacheStop[slot]);
            } else {
               if (!silent)
                  s+="NULL";
            }
         }
         if (!silent) {
            for (unsigned index=0;index<count;index++)
               std::cout << s << std::endl;
         }
      } while ((count=input->next())!=0);
   } else if (duplicateHandling==ShowDuplicates) {
      // Show only duplicates?
      do {
         if (count<=1) continue;
         bool first=true;
         for (std::vector<Register*>::const_iterator iter=output.begin(),limit=output.end();iter!=limit;++iter) {
            if (first) first=false; else if (!silent) std::cout << ' ';
            if (~((*iter)->value)) {
               unsigned value=(*iter)->value,slot=value%cacheSize;
               if (constantCache[slot]!=value) {
                  constantCache[slot]=value;
                  dictionary.lookupById(value,cacheStart[slot],cacheStop[slot]);
               }
               if (!silent)
                  std::cout << std::string(cacheStart[slot],cacheStop[slot]);
            } else {
               if (!silent)
                  std::cout << "NULL";
            }
         }
         if (!silent) {
            std::cout << " x" << count << std::endl;
         }
      } while ((count=input->next())!=0);
   } else {
      // No, reduce or count duplicates
      do {
         bool first=true;
         for (std::vector<Register*>::const_iterator iter=output.begin(),limit=output.end();iter!=limit;++iter) {
            if (first) first=false; else if (!silent) std::cout << ' ';
            if (~((*iter)->value)) {
               unsigned value=(*iter)->value,slot=value%cacheSize;
               if (constantCache[slot]!=value) {
                  constantCache[slot]=value;
                  dictionary.lookupById(value,cacheStart[slot],cacheStop[slot]);
               }
               if (!silent)
                  std::cout << std::string(cacheStart[slot],cacheStop[slot]);
            } else {
               if (!silent)
                  std::cout << "NULL";
            }
         }
         if (!silent) {
            if ((count!=1)&&(duplicateHandling==CountDuplicates))
               std::cout << " x" << count;
            std::cout << std::endl;
         }
      } while ((count=input->next())!=0);
   }

   return 1;
}
//---------------------------------------------------------------------------
unsigned ResultsPrinter::next()
   // Produce the next tuple
{
   return false;
}
//---------------------------------------------------------------------------
void ResultsPrinter::print(unsigned level)
   // Print the operator tree. Debugging only.
{
   indent(level); std::cout << "<ResultsPrinter";
   for (std::vector<Register*>::const_iterator iter=output.begin(),limit=output.end();iter!=limit;++iter) {
      std::cout << " "; printRegister(*iter);
   }
   std::cout << std::endl;
   input->print(level+1);
   indent(level); std::cout << ">" << std::endl;
}
//---------------------------------------------------------------------------
