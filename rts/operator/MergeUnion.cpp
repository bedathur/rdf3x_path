#include "rts/operator/MergeUnion.hpp"
#include "rts/runtime/Runtime.hpp"
#include <iostream>
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
MergeUnion::MergeUnion(Register* result,Operator* left,Register* leftReg,Operator* right,Register* rightReg)
   : left(left),right(right),leftReg(leftReg),rightReg(rightReg),result(result)
   // Constructor
{
}
//---------------------------------------------------------------------------
MergeUnion::~MergeUnion()
   // Destructor
{
   delete left;
   delete right;
}
//---------------------------------------------------------------------------
unsigned MergeUnion::first()
   // Produce the first tuple
{
   // Read the first tuple on the left side
   if ((leftCount=left->first())==0) {
      if ((rightCount=right->first())==0) {
         state=done;
         return false;
      }
      result->value=rightReg->value;
      state=leftEmpty;
      return rightCount;
   }
   leftValue=leftReg->value;

   // Read the first tuple on the right side
   if ((rightCount=right->first())==0) {
      result->value=leftReg->value;
      state=rightEmpty;
      return leftCount;
   }
   rightValue=rightReg->value;

   // Handle the cases
   if (leftValue<rightValue) {
      result->value=leftValue;
      state=stepLeft;
      return leftCount;
   } else if (leftValue>rightValue) {
      result->value=rightValue;
      state=stepRight;
      return rightCount;
   } else {
      result->value=leftValue;
      state=stepBoth;
      return leftCount+rightCount;
   }
}
//---------------------------------------------------------------------------
unsigned MergeUnion::next()
   // Produce the next tuple
{
   switch (state) {
      case done: return false;
      case stepLeft: case stepBoth:
         if ((leftCount=left->next())==0) {
            result->value=rightValue;
            state=leftEmpty;
            return rightCount;
         }
         leftValue=leftReg->value;
         // Fallthrough
      case stepRight:
         if (state!=stepLeft) {
            if ((rightCount=right->next())==0) {
               result->value=leftValue;
               state=rightEmpty;
               return leftCount;
            }
            rightValue=rightReg->value;
         }
         // Handle the cases
         if (leftValue<rightValue) {
            result->value=leftValue;
            state=stepLeft;
            return leftCount;
         } else if (leftValue>rightValue) {
            result->value=rightValue;
            state=stepRight;
            return rightCount;
         } else {
            result->value=leftValue;
            state=stepBoth;
            return leftCount+rightCount;
         }
      case leftEmpty:
         if ((rightCount=right->next())==0) {
            state=done;
            return false;
         }
         result->value=rightReg->value;
         return rightCount;
      case rightEmpty:
         if ((leftCount=left->next())==0) {
            state=done;
            return false;
         }
         result->value=leftReg->value;
         return leftCount;
   }
   return false;
}
//---------------------------------------------------------------------------
void MergeUnion::print(unsigned level)
   // Print the operator tree. Debugging only.
{
   indent(level); std::cout << "<MergeUnion ";
   printRegister(result);
   std::cout << " [";
   printRegister(leftReg);
   std::cout << " ";
   printRegister(rightReg);
   std::cout << "]" << std::endl;
   left->print(level+1);
   right->print(level+1);
   indent(level); std::cout << ">" << std::endl;
}
//---------------------------------------------------------------------------
