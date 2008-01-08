#include "rts/operator/NestedLoopFilter.hpp"
#include "rts/runtime/Runtime.hpp"
#include <algorithm>
#include <iostream>
//---------------------------------------------------------------------------
NestedLoopFilter::NestedLoopFilter(Operator* input,Register* filter,const std::vector<unsigned>& values)
   : input(input),filter(filter),values(values)
   // Constructor
{
   std::sort(this->values.begin(),this->values.end());
}
//---------------------------------------------------------------------------
NestedLoopFilter::~NestedLoopFilter()
   // Destructor
{
}
//---------------------------------------------------------------------------
unsigned NestedLoopFilter::first()
   // Produce the first tuple
{
   for (pos=0;pos<values.size();++pos) {
      filter->value=values[pos];
      unsigned count;
      if ((count=input->first())!=0)
         return count;
   }
   return false;
}
//---------------------------------------------------------------------------
unsigned NestedLoopFilter::next()
   // Produce the next tuple
{
   // Done?
   if (pos>=values.size())
      return false;

   // More tuples?
   unsigned count;
   if ((count=input->next())!=0)
      return count;

   // No, go to the next value
   for (++pos;pos<values.size();++pos) {
      filter->value=values[pos];
      unsigned count;
      if ((count=input->first())!=0)
         return count;
   }
   return false;
}
//---------------------------------------------------------------------------
void NestedLoopFilter::print(unsigned level)
   // Print the operator tree. Debugging only.
{
   indent(level); std::cout << "<NestedLoopFilter ";
   printRegister(filter);
   std::cout << " [";
   for (std::vector<unsigned>::const_iterator iter=values.begin(),limit=values.end();iter!=limit;++iter) {
      std::cout << " " << (*iter);
   }
   std::cout << "]" << std::endl;
   input->print(level+1);
   indent(level); std::cout << ">" << std::endl;
}
//---------------------------------------------------------------------------
