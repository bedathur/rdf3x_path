#include "cts/codegen/CodeGen.hpp"
#include "cts/infra/QueryGraph.hpp"
#include "cts/plangen/Plan.hpp"
#include "rts/operator/AggregatedIndexScan.hpp"
#include "rts/operator/EmptyScan.hpp"
#include "rts/operator/Filter.hpp"
#include "rts/operator/FullyAggregatedIndexScan.hpp"
#include "rts/operator/HashGroupify.hpp"
#include "rts/operator/HashJoin.hpp"
#include "rts/operator/FastDijkstraScan.hpp"
#include "rts/operator/RegularPathScan.hpp"
#include "rts/operator/DescribeScan.hpp"
#include "rts/operator/DijkstraScan.hpp"
#include "rts/operator/IndexScan.hpp"
#include "rts/operator/MergeJoin.hpp"
#include "rts/operator/MergeUnion.hpp"
#include "rts/operator/NestedLoopFilter.hpp"
#include "rts/operator/NestedLoopJoin.hpp"
#include "rts/operator/ResultsPrinter.hpp"
#include "rts/operator/Selection.hpp"
#include "rts/operator/SingletonScan.hpp"
#include "rts/operator/Sort.hpp"
#include "rts/operator/TableFunction.hpp"
#include "rts/operator/Union.hpp"
#include "rts/runtime/Runtime.hpp"
#include "rts/runtime/DifferentialIndex.hpp"
#include <cstdlib>
#include <map>
#include <set>
#include <cassert>
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
using namespace std;
//---------------------------------------------------------------------------
/// Structure for bindings, we use either single-value binding with Register, or vector-value binding with Vector Register
struct Binding{
   map<unsigned,Register*> valuebinding;
   map<unsigned,VectorRegister*> pathbinding;
};
//---------------------------------------------------------------------------
/// Structure for registers, we separate registers for single values and for paths
struct MapRegister{
   map<const QueryGraph::Node*,unsigned> valueregister;
   map<const QueryGraph::Node*,unsigned> pathregister;
};
//---------------------------------------------------------------------------
static Operator* translatePlan(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari);
//---------------------------------------------------------------------------
static void resolveScanVariable(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,map<unsigned,Register*>& bindings,const map<const QueryGraph::Node*,unsigned>& registers,unsigned slot,const QueryGraph::Node& node,Register*& reg,bool& bound,bool unused=false)
   // Resolve a variable used in a scan
{
   bool constant=(slot==0)?node.constSubject:((slot==1)?node.constPredicate:node.constObject);
   unsigned var=(slot==0)?node.subject:((slot==1)?node.predicate:node.object);
   reg=runtime.getRegister((*registers.find(&node)).second+slot);
   if (constant) {
      bound=true;
      reg->value=var;
   } else if (unused) {
      bound=false;
      reg=0;
   } else {
      if (context.count(var)) {
         bound=true;
         reg=(*(context.find(var))).second;
      } else {
         bound=false;
         if (projection.count(var)){
            bindings[var]=reg;
         }
      }
   }
}
//---------------------------------------------------------------------------
static void resolvePathScanVariable(Runtime& runtime, const set<unsigned>& pathprojection,map<unsigned,VectorRegister*>& pathbindings,const map<const QueryGraph::Node*,unsigned>& pathregisters, const QueryGraph::Node& node, VectorRegister*& reg, bool& bound)
// resolve a path variable used in a path scan
{
	assert(node.pathTriple);
	bool constant=node.constPredicate;
	unsigned var=node.predicate;
	reg=runtime.getVectorRegister((*pathregisters.find(&node)).second);
	if (constant){
		/// constant paths are not supported at present
		bound=true;
	} else {
		bound = false;
		if (pathprojection.count(var)){
			pathbindings[var]=reg;
		}
	}
}
//---------------------------------------------------------------------------
static Operator* translateDijkstraScan(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter)
// Translate a path scan into an operator tree
{
	const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right);

	// plan that computes start/stop of the scan
	Operator* subplan=0;
	Register* pathnode=0;
	Database::DataOrder order=static_cast<Database::DataOrder>(plan->opArg);

	if (plan->left){
		 if (order==Database::Order_Object_Predicate_Subject){
			set<unsigned> projection;
			projection.insert(node.object);
			map<unsigned,Index*> ferrari;
			subplan=translatePlan(runtime,context,projection,bindings,registers,plan->left,pathfilter,ferrari);
			pathnode=bindings.valuebinding[node.object];
		}
	}

	// Initialize the registers
	bool constSubject,constPredicate,constObject;
	Register* subject,*object;
	VectorRegister* pathpredicate;
	resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,0,node,subject,constSubject);
	resolvePathScanVariable(runtime,projection,bindings.pathbinding,registers.pathregister,node,pathpredicate,constPredicate);
	resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,2,node,object,constObject);

	//  return the operator
	return FastDijkstraScan::create(runtime.getDatabase(),static_cast<Database::DataOrder>(plan->opArg),
	                         subject,constSubject,
	                         pathpredicate,constPredicate,
	                         object,constObject,
		    plan->cardinality,pathnode,subplan,pathfilter);
}
//---------------------------------------------------------------------------
static Operator* translateRegularPathScan(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* /*pathfilter*/,map<unsigned,Index*>& ferrari)
// Translate a reachability check into an operator tree
{
   const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right);
   // Initialize the registers
   bool constSubject,constObject;
   Register* subject,*object;
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,0,node,subject,constSubject);
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,2,node,object,constObject);
   RegularPathScan::Modifier mod=RegularPathScan::Add;
   if (node.pathmod==QueryGraph::Node::Mul){
   	mod=RegularPathScan::Mul;
   }
   // Construct the operator
   return RegularPathScan::create(runtime.getDatabase(),static_cast<Database::DataOrder>(plan->opArg),
   										subject,constSubject,object,constObject,plan->cardinality,mod,node.predicate,ferrari[node.predicate]);
}
//---------------------------------------------------------------------------
static Operator* translateIndexScan(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* /*pathfilter*/)
   // Translate an index scan into an operator tree
{
   const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right);

   // Initialize the registers
   bool constSubject,constPredicate,constObject;
   Register* subject,*predicate,*object;
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,0,node,subject,constSubject);
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,1,node,predicate,constPredicate);
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,2,node,object,constObject);
   // And return the operator
   if (runtime.hasDifferentialIndex())
      return runtime.getDifferentialIndex().createScan(static_cast<Database::DataOrder>(plan->opArg),subject,constSubject,predicate,constPredicate,object,constObject,plan->cardinality);
   return IndexScan::create(runtime.getDatabase(),static_cast<Database::DataOrder>(plan->opArg),
                            subject,constSubject,
                            predicate,constPredicate,
                            object,constObject,
			    plan->cardinality);
}
//---------------------------------------------------------------------------
static Operator* translateAggregatedIndexScan(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* /*pathfilter*/)
   // Translate an aggregated index scan into an operator tree
{
   const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right);
   Database::DataOrder order=static_cast<Database::DataOrder>(plan->opArg);

   // Initialize the registers
   bool constSubject,constPredicate,constObject;
   Register* subject,*predicate,*object;
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,0,node,subject,constSubject,(order==Database::Order_Object_Predicate_Subject)||(order==Database::Order_Predicate_Object_Subject));
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,1,node,predicate,constPredicate,(order==Database::Order_Subject_Object_Predicate)||(order==Database::Order_Object_Subject_Predicate));
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,2,node,object,constObject,(order==Database::Order_Subject_Predicate_Object)||(order==Database::Order_Predicate_Subject_Object));

   // And return the operator
   if (runtime.hasDifferentialIndex())
      return runtime.getDifferentialIndex().createAggregatedScan(static_cast<Database::DataOrder>(plan->opArg),subject,constSubject,predicate,constPredicate,object,constObject,plan->cardinality);
   return AggregatedIndexScan::create(runtime.getDatabase(),order,
                                      subject,constSubject,
                                      predicate,constPredicate,
                                      object,constObject,
				      plan->cardinality);
}
//---------------------------------------------------------------------------
static Operator* translateFullyAggregatedIndexScan(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* /*pathfilter*/)
   // Translate an fully aggregated index scan into an operator tree
{
   const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right);
   Database::DataOrder order=static_cast<Database::DataOrder>(plan->opArg);

   // Initialize the registers
   bool constSubject,constPredicate,constObject;
   Register* subject,*predicate,*object;
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,0,node,subject,constSubject,(order!=Database::Order_Subject_Predicate_Object)&&(order!=Database::Order_Subject_Object_Predicate));
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,1,node,predicate,constPredicate,(order!=Database::Order_Predicate_Subject_Object)&&(order!=Database::Order_Predicate_Object_Subject));
   resolveScanVariable(runtime,context,projection,bindings.valuebinding,registers.valueregister,2,node,object,constObject,(order!=Database::Order_Object_Subject_Predicate)&&(order!=Database::Order_Object_Predicate_Subject));

   // And return the operator
   if (runtime.hasDifferentialIndex())
      return runtime.getDifferentialIndex().createFullyAggregatedScan(static_cast<Database::DataOrder>(plan->opArg),subject,constSubject,predicate,constPredicate,object,constObject,plan->cardinality);
   return FullyAggregatedIndexScan::create(runtime.getDatabase(),order,
                                           subject,constSubject,
                                           predicate,constPredicate,
                                           object,constObject,
					   plan->cardinality);
}
//---------------------------------------------------------------------------
static void collectVariables(const map<unsigned,Register*>& context,set<unsigned>& variables,Plan* plan)
   // Collect all variables contained in a plan
{
   switch (plan->op) {
      case Plan::IndexScan:
      case Plan::AggregatedIndexScan:
      case Plan::FullyAggregatedIndexScan:
      case Plan::DijkstraScan:
      case Plan::RegularPath:{
         const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right);
         if ((!node.constSubject)&&(!context.count(node.subject)))
            variables.insert(node.subject);
         if ((!node.constPredicate)&&(!context.count(node.predicate)))
            variables.insert(node.predicate);
         if ((!node.constObject)&&(!context.count(node.object)))
            variables.insert(node.object);
         break;
      }
      case Plan::NestedLoopJoin:
      case Plan::MergeJoin:
      case Plan::HashJoin:
      case Plan::Union:
      case Plan::MergeUnion:
         collectVariables(context,variables,plan->left);
         collectVariables(context,variables,plan->right);
         break;
      case Plan::HashGroupify:
      case Plan::Filter:
      case Plan::PathFilter:
         collectVariables(context,variables,plan->left);
         break;
      case Plan::TableFunction: {
         const QueryGraph::TableFunction& func=*reinterpret_cast<QueryGraph::TableFunction*>(plan->right);
         for (vector<unsigned>::const_iterator iter=func.output.begin(),limit=func.output.end();iter!=limit;++iter)
            variables.insert(*iter);
         collectVariables(context,variables,plan->left);
         break;
      }
      case Plan::Singleton:
         break;
   }
}
//---------------------------------------------------------------------------
static void getJoinVariables(const map<unsigned,Register*>& context,set<unsigned>& variables,Plan* left,Plan* right)
   // Get the join variables
{
   // Collect all variables
   set<unsigned> leftVariables,rightVariables;
   collectVariables(context,leftVariables,left);
   collectVariables(context,rightVariables,right);

//   cerr<<"left vars: ";
//   for (auto t: leftVariables) cerr<<t<<" ";
//   cerr<<endl;
//
//   cerr<<"right vars: ";
//   for (auto t: rightVariables) cerr<<t<<" ";
//   cerr<<endl;

   // Find common ones
   if (leftVariables.size()<rightVariables.size()) {
      for (set<unsigned>::const_iterator iter=leftVariables.begin(),limit=leftVariables.end();iter!=limit;++iter)
         if (rightVariables.count(*iter))
            variables.insert(*iter);
   } else {
      for (set<unsigned>::const_iterator iter=rightVariables.begin(),limit=rightVariables.end();iter!=limit;++iter)
         if (leftVariables.count(*iter))
            variables.insert(*iter);
   }
}
//---------------------------------------------------------------------------
static void mergeBindings(const set<unsigned>& projection,Binding& bindings,const Binding& leftBindings,const Binding& rightBindings)
   // Merge bindings after a join
{
   for (map<unsigned,Register*>::const_iterator iter=leftBindings.valuebinding.begin(),limit=leftBindings.valuebinding.end();iter!=limit;++iter){
      if (projection.count((*iter).first))
         bindings.valuebinding[(*iter).first]=(*iter).second;
   }
   for (map<unsigned,VectorRegister*>::const_iterator iter=leftBindings.pathbinding.begin(),limit=leftBindings.pathbinding.end();iter!=limit;++iter){
      if (projection.count((*iter).first))
    	  bindings.pathbinding[(*iter).first]=(*iter).second;
   }
   for (map<unsigned,Register*>::const_iterator iter=rightBindings.valuebinding.begin(),limit=rightBindings.valuebinding.end();iter!=limit;++iter){
      if (projection.count((*iter).first)&&(!bindings.valuebinding.count((*iter).first)))
         bindings.valuebinding[(*iter).first]=(*iter).second;
   }
   for (map<unsigned,VectorRegister*>::const_iterator iter=rightBindings.pathbinding.begin(),limit=rightBindings.pathbinding.end();iter!=limit;++iter){
      if (projection.count((*iter).first))
    	  bindings.pathbinding[(*iter).first]=(*iter).second;
   }

}
//---------------------------------------------------------------------------
static Operator* addAdditionalSelections(Runtime& runtime,Operator* input,const set<unsigned>& joinVariables,Binding& leftBindings,Binding& rightBindings,unsigned joinedOn)
   // Convert additional join predicates into a selection
{
   // Examine join conditions
   vector<Register*> left,right;
   for (set<unsigned>::const_iterator iter=joinVariables.begin(),limit=joinVariables.end();iter!=limit;++iter) {
      if ((*iter)!=joinedOn) {
         left.push_back(leftBindings.valuebinding[*iter]);
         right.push_back(rightBindings.valuebinding[*iter]);
      }
   }

   // Build the results
   if (!left.empty()) {
      Selection::Predicate* predicate=0;
      for (unsigned index=0;index<left.size();index++) {
         Selection::Predicate* p=new Selection::Equal(new Selection::Variable(left[index]),new Selection::Variable(right[index]));
         if (predicate)
            predicate=new Selection::And(predicate,p); else
            predicate=p;
      }
      return new Selection(input,runtime,predicate,input->getExpectedOutputCardinality());
   } else  {
      return input;
   }
}
//---------------------------------------------------------------------------
static Operator* translateNestedLoopJoin(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a nested loop join into an operator tree
{
   // Get the join variables (if any)
   set<unsigned> joinVariables,newProjection=projection;
   getJoinVariables(context,joinVariables,plan->left,plan->right);
   newProjection.insert(joinVariables.begin(),joinVariables.end());

   // Build the input trees
   Binding leftBindings,rightBindings;

   Operator* leftTree=translatePlan(runtime,context,newProjection,leftBindings,registers,plan->left,pathfilter,ferrari);
   Operator* rightTree=translatePlan(runtime,context,newProjection,rightBindings,registers,plan->right,pathfilter,ferrari);
   mergeBindings(projection,bindings,leftBindings,rightBindings);

   // Build the operator
   Operator* result=new NestedLoopJoin(leftTree,rightTree,plan->cardinality);

   // And apply additional selections if necessary
   result=addAdditionalSelections(runtime,result,joinVariables,leftBindings,rightBindings,~0u);

   return result;
}
//---------------------------------------------------------------------------
static void setRegularPathSubtree(Operator* result, Operator* subTree, vector<Register*> tail, Register* r,unsigned slot){
	RegularPathScan* regularResult= dynamic_cast<RegularPathScan*>(result);
	switch(slot){
	case 0:
		regularResult->setFirstInput(subTree);
		regularResult->setFirstBinding(tail);
		regularResult->setFirstSource(r);
		break;
	case 2:
		regularResult->setSecondInput(subTree);
		regularResult->setSecondBinding(tail);
		regularResult->setSecondSource(r);
		break;
	}

	if (regularResult->isFirstInputSet()&&regularResult->isSecondInputSet()){
		regularResult->checkAndSwap();
	}
}
//---------------------------------------------------------------------------
static Operator* translateMergeJoin(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan*& plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a merge join into an operator tree
{
   // Get the join variables (if any)
   set<unsigned> joinVariables,newProjection=projection;
   getJoinVariables(context,joinVariables,plan->left,plan->right);
   newProjection.insert(joinVariables.begin(),joinVariables.end());
   assert(!joinVariables.empty());
   unsigned joinOn=plan->opArg;
   assert(joinVariables.count(joinOn));

   // Build the input trees
   Binding leftBindings,rightBindings;
   Operator* leftTree=translatePlan(runtime,context,newProjection,leftBindings,registers,plan->left,pathfilter,ferrari);
   Operator* rightTree=translatePlan(runtime,context,newProjection,rightBindings,registers,plan->right,pathfilter,ferrari);
   assert(rightTree);
   assert(leftTree);
   mergeBindings(projection,bindings,leftBindings,rightBindings);

   Operator* result = 0;

	// Prepare the tails
	vector<Register*> leftTail,rightTail;
	for (map<unsigned,Register*>::const_iterator iter=leftBindings.valuebinding.begin(),limit=leftBindings.valuebinding.end();iter!=limit;++iter)
		if ((*iter).first!=joinOn)
			leftTail.push_back((*iter).second);
	for (map<unsigned,Register*>::const_iterator iter=rightBindings.valuebinding.begin(),limit=rightBindings.valuebinding.end();iter!=limit;++iter)
		if ((*iter).first!=joinOn)
			rightTail.push_back((*iter).second);

   if (plan->left->op==Plan::RegularPath){
      const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->left->right);

   	result=leftTree;
   	plan->op=Plan::RegularPath;
   	plan->right=plan->left->right;
   	unsigned slot=(node.subject==joinOn)?0:2;
   	setRegularPathSubtree(leftTree,rightTree,rightTail,rightBindings.valuebinding[joinOn],slot);
   } else if (plan->right->op==Plan::RegularPath){
      const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right->right);

   	result=rightTree;
   	plan->op=Plan::RegularPath;
   	plan->right=plan->right->right;
   	unsigned slot=(node.subject==joinOn)?0:2;

   	setRegularPathSubtree(rightTree,leftTree,leftTail,leftBindings.valuebinding[joinOn],slot);
   } else {
   	// Build the operator
   	result=new MergeJoin(leftTree,leftBindings.valuebinding[joinOn],leftTail,rightTree,rightBindings.valuebinding[joinOn],rightTail,plan->cardinality);

   	// And apply additional selections if necessary
   	result=addAdditionalSelections(runtime,result,joinVariables,leftBindings,rightBindings,joinOn);
   }
   return result;
}
//---------------------------------------------------------------------------
static Operator* translateHashJoin(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan*& plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a hash join into an operator tree
{
   // Get the join variables (if any)
   set<unsigned> joinVariables,newProjection=projection;
   getJoinVariables(context,joinVariables,plan->left,plan->right);
   newProjection.insert(joinVariables.begin(),joinVariables.end());
   assert(!joinVariables.empty());
   unsigned joinOn=*(joinVariables.begin());

   // Build the input trees
   Binding leftBindings,rightBindings;
   Operator* leftTree=translatePlan(runtime,context,newProjection,leftBindings,registers,plan->left,pathfilter,ferrari);
   Operator* rightTree=translatePlan(runtime,context,newProjection,rightBindings,registers,plan->right,pathfilter,ferrari);
   mergeBindings(projection,bindings,leftBindings,rightBindings);

   // Prepare the tails
   vector<Register*> leftTail,rightTail;
   Register *regJoin=0;
   for (map<unsigned,Register*>::const_iterator iter=leftBindings.valuebinding.begin(),limit=leftBindings.valuebinding.end();iter!=limit;++iter)
      if ((*iter).first!=joinOn)
         leftTail.push_back((*iter).second);
      else
      	regJoin=(*iter).second;
   for (map<unsigned,Register*>::const_iterator iter=rightBindings.valuebinding.begin(),limit=rightBindings.valuebinding.end();iter!=limit;++iter)
      if ((*iter).first!=joinOn)
         rightTail.push_back((*iter).second);

   // Build the operator
   Operator* result=0;

   if (plan->left->op==Plan::RegularPath){
   	result=leftTree;
      const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->left->right);
   	plan->op=Plan::RegularPath;
   	plan->right=plan->left->right;
   	unsigned slot=(node.subject==joinOn)?0:2;

   	setRegularPathSubtree(result,rightTree,rightTail,rightBindings.valuebinding[joinOn],slot);
   }
   else if (plan->right->op==Plan::RegularPath){
   	result=rightTree;
      const QueryGraph::Node& node=*reinterpret_cast<QueryGraph::Node*>(plan->right->right);
   	plan->right=plan->right->right;
   	plan->op=Plan::RegularPath;
   	unsigned slot=(node.subject==joinOn)?0:2;

   	setRegularPathSubtree(result,leftTree,leftTail,leftBindings.valuebinding[joinOn],slot);
   } else {
   	result=new HashJoin(leftTree,leftBindings.valuebinding[joinOn],leftTail,rightTree,rightBindings.valuebinding[joinOn],rightTail,-plan->left->costs,plan->right->costs,plan->cardinality);

   	// And apply additional selections if necessary
   	result=addAdditionalSelections(runtime,result,joinVariables,leftBindings,rightBindings,joinOn);
   }
   return result;
}
//---------------------------------------------------------------------------
static Operator* translateHashGroupify(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a hash groupify into an operator tree
{
   // Build the input trees
   Operator* tree=translatePlan(runtime,context,projection,bindings,registers,plan->left,pathfilter,ferrari);

   // Collect output registers
   vector<Register*> output;
   for (map<unsigned,Register*>::const_iterator iter=bindings.valuebinding.begin(),limit=bindings.valuebinding.end();iter!=limit;++iter)
      output.push_back((*iter).second);

   // Build the operator
   return new HashGroupify(tree,output,plan->cardinality);
}
//---------------------------------------------------------------------------
static void collectVariables(set<unsigned>& filterVariables,const QueryGraph::Filter& filter)
   // Collect all query variables
{
   if (filter.type==QueryGraph::Filter::Variable||filter.type==QueryGraph::Filter::PathVariable)
      filterVariables.insert(filter.id);
   if (filter.arg1)
      collectVariables(filterVariables,*filter.arg1);
   if (filter.arg2)
      collectVariables(filterVariables,*filter.arg2);
   if (filter.arg3)
      collectVariables(filterVariables,*filter.arg3);
}
//---------------------------------------------------------------------------
static Selection::Predicate* buildSelection(const Binding& bindings,const QueryGraph::Filter& filter);
//---------------------------------------------------------------------------
static void collectSelectionArgs(const Binding& bindings,vector<Selection::Predicate*>& args,const QueryGraph::Filter* input)
   // Collect all function arguments
{
   for (const QueryGraph::Filter* iter=input;iter;iter=iter->arg2) {
      assert(iter->type==QueryGraph::Filter::ArgumentList);
      args.push_back(buildSelection(bindings,*(iter->arg1)));
   }
}
//---------------------------------------------------------------------------
static Selection::Predicate* buildSelection(const Binding& bindings,const QueryGraph::Filter& filter)
   // Construct a complex filter predicate
{
   switch (filter.type) {
      case QueryGraph::Filter::Or: return new Selection::Or(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::And: return new Selection::And(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Equal: return new Selection::Equal(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::NotEqual: return new Selection::And(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Less: return new Selection::Less(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::LessOrEqual: return new Selection::LessOrEqual(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Greater: return new Selection::Less(buildSelection(bindings,*filter.arg2),buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::GreaterOrEqual: return new Selection::LessOrEqual(buildSelection(bindings,*filter.arg2),buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Plus: return new Selection::Plus(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Minus: return new Selection::Minus(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Mul: return new Selection::Mul(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Div: return new Selection::Div(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Not: return new Selection::Not(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::UnaryPlus: return buildSelection(bindings,*filter.arg1);
      case QueryGraph::Filter::UnaryMinus: return new Selection::Neg(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Literal:
         if (~filter.id)
            return new Selection::ConstantLiteral(filter.id); else
            return new Selection::TemporaryConstantLiteral(filter.value);
      case QueryGraph::Filter::Variable:
         if (bindings.valuebinding.count(filter.id))
            return new Selection::Variable((*bindings.valuebinding.find(filter.id)).second); else
            return new Selection::Null();
      case QueryGraph::Filter::IRI:
         if (~filter.id)
            return new Selection::ConstantIRI(filter.id); else
            return new Selection::TemporaryConstantIRI(filter.value);
      case QueryGraph::Filter::Null: return new Selection::Null();
      case QueryGraph::Filter::Function: {
         assert(filter.arg1->type==QueryGraph::Filter::IRI);
         vector<Selection::Predicate*> args;
         collectSelectionArgs(bindings,args,filter.arg2);
         return new Selection::FunctionCall(filter.arg1->value,args); }
      case QueryGraph::Filter::ArgumentList: assert(false); // cannot happen
      case QueryGraph::Filter::Builtin_str: return new Selection::BuiltinStr(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Builtin_lang: return new Selection::BuiltinLang(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Builtin_langmatches: return new Selection::BuiltinLangMatches(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Builtin_datatype: return new Selection::BuiltinDatatype(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Builtin_bound:
         if (~filter.id)
            return new Selection::BuiltinBound((*bindings.valuebinding.find(filter.id)).second); else
            return new Selection::False();
      case QueryGraph::Filter::Builtin_sameterm: return new Selection::BuiltinSameTerm(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2));
      case QueryGraph::Filter::Builtin_isiri: return new Selection::BuiltinIsIRI(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Builtin_isblank: return new Selection::BuiltinIsBlank(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Builtin_isliteral: return new Selection::BuiltinIsLiteral(buildSelection(bindings,*filter.arg1));
      case QueryGraph::Filter::Builtin_regex: return new Selection::BuiltinRegEx(buildSelection(bindings,*filter.arg1),buildSelection(bindings,*filter.arg2),filter.arg3?buildSelection(bindings,*filter.arg3):0);
      case QueryGraph::Filter::Builtin_in: {
         vector<Selection::Predicate*> args;
         collectSelectionArgs(bindings,args,filter.arg2);
         return new Selection::BuiltinIn(buildSelection(bindings,*filter.arg1),args); }
      case QueryGraph::Filter::Builtin_length:
      case QueryGraph::Filter::Builtin_containsany:
      case QueryGraph::Filter::PathVariable:
      case QueryGraph::Filter::Builtin_containsonly:
    	  break; //does not happen
   }
   throw; // Cannot happen
}
//---------------------------------------------------------------------------
static Operator* translatePathFilter(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* /*pathfilter*/,map<unsigned,Index*>& ferrari)
// Translate a path filter into an operator tree
{
   QueryGraph::Filter* filter=reinterpret_cast<QueryGraph::Filter*>(plan->right);
   // Collect path variables
   set<unsigned> filterVariables;
   collectVariables(filterVariables,*filter);
   set<unsigned> newProjection=projection;
   for (set<unsigned>::const_iterator iter=filterVariables.begin(),limit=filterVariables.end();iter!=limit;++iter)
      newProjection.insert(*iter);
   Operator* tree=translatePlan(runtime,context,newProjection,bindings,registers,plan->left,filter,ferrari);
   return tree;
}
//---------------------------------------------------------------------------
static Operator* translateFilter(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a filter into an operator tree
{
   const QueryGraph::Filter& filter=*reinterpret_cast<QueryGraph::Filter*>(plan->right);

   // Collect all variables
   set<unsigned> filterVariables;
   collectVariables(filterVariables,filter);

   // Build the input trees
   set<unsigned> newProjection=projection;
   for (set<unsigned>::const_iterator iter=filterVariables.begin(),limit=filterVariables.end();iter!=limit;++iter)
      newProjection.insert(*iter);
   Operator* tree=translatePlan(runtime,context,newProjection,bindings,registers,plan->left,pathfilter,ferrari);

   // Build the operator, try special cases first
   Operator* result=0;
   if (((filter.type==QueryGraph::Filter::Equal)||(filter.type==QueryGraph::Filter::NotEqual))&&(filter.arg1->type==QueryGraph::Filter::Variable)) {
      if (((filter.arg2->type==QueryGraph::Filter::Literal)||(filter.arg2->type==QueryGraph::Filter::IRI))&&(bindings.valuebinding.count(filter.arg1->id))) {
         vector<unsigned> values;
         values.push_back(filter.arg2->id);
         result=new Filter(tree,bindings.valuebinding[filter.arg1->id],values,filter.type==QueryGraph::Filter::NotEqual,plan->cardinality);
      }
   }
   if ((!result)&&((filter.type==QueryGraph::Filter::Equal)||(filter.type==QueryGraph::Filter::NotEqual))&&(filter.arg2->type==QueryGraph::Filter::Variable)) {
      if (((filter.arg1->type==QueryGraph::Filter::Literal)||(filter.arg1->type==QueryGraph::Filter::IRI))&&(bindings.valuebinding.count(filter.arg2->id))) {
         vector<unsigned> values;
         values.push_back(filter.arg1->id);
         result=new Filter(tree,bindings.valuebinding[filter.arg2->id],values,filter.type==QueryGraph::Filter::NotEqual,plan->cardinality);
      }
   }
   if ((!result)&&(filter.type==QueryGraph::Filter::Builtin_in)&&(filter.arg1->type==QueryGraph::Filter::Variable)&&(bindings.valuebinding.count(filter.arg1->id))) {
      vector<unsigned> values;
      bool valid=true;
      if (filter.arg2) {
         const QueryGraph::Filter* iter=filter.arg2;
         for (;iter;iter=iter->arg2) {
            assert(iter->type==QueryGraph::Filter::ArgumentList);
            if ((iter->arg1->type!=QueryGraph::Filter::Literal)&&(iter->arg1->type!=QueryGraph::Filter::IRI)) {
               valid=false;
               break;
            }
            values.push_back(iter->arg1->id);
         }
      }
      if (valid) {
         result=new Filter(tree,bindings.valuebinding[filter.arg1->id],values,false,plan->cardinality);
      }
   }
   if (!result) {
      result=new Selection(tree,runtime,buildSelection(bindings,filter),plan->cardinality);
   }

   // Cleanup the binding
   for (set<unsigned>::const_iterator iter=filterVariables.begin(),limit=filterVariables.end();iter!=limit;++iter)
      if (!projection.count(*iter))
         bindings.valuebinding.erase(*iter);

   return result;
}
//---------------------------------------------------------------------------
static Operator* translateUnion(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a union into an operator tree
{
   // Collect the parts
   vector<Plan*> parts;
   while (true) {
      parts.push_back(plan->left);
      if (plan->right->op!=Plan::Union) {
         parts.push_back(plan->right);
         break;
      } else plan=plan->right;
   }

   // Translate the parts of the union
   vector<Binding> subBindings;
   vector<Operator*> trees;
   subBindings.resize(parts.size());
   trees.resize(parts.size());
   for (unsigned index=0;index<parts.size();index++)
      trees[index]=translatePlan(runtime,context,projection,subBindings[index],registers,parts[index],pathfilter,ferrari);

   // Collect all bindings
   for (vector<Binding>::const_iterator iter=subBindings.begin(),limit=subBindings.end();iter!=limit;++iter)
      for (map<unsigned,Register*>::const_iterator iter2=(*iter).valuebinding.begin(),limit2=(*iter).valuebinding.end();iter2!=limit2;++iter2)
         if (!bindings.valuebinding.count((*iter2).first))
            bindings.valuebinding[(*iter2).first]=(*iter2).second;

   // Construct the mappings and initializations
   vector<vector<Register*> > mappings,initializations;
   mappings.resize(parts.size());
   initializations.resize(parts.size());
   for (unsigned index=0;index<subBindings.size();index++) {
      for (map<unsigned,Register*>::const_iterator iter=subBindings[index].valuebinding.begin(),limit=subBindings[index].valuebinding.end();iter!=limit;++iter)
         if (bindings.valuebinding[(*iter).first]!=(*iter).second) {
            mappings[index].push_back((*iter).second);
            mappings[index].push_back(bindings.valuebinding[(*iter).first]);
         }
      for (map<unsigned,Register*>::const_iterator iter=bindings.valuebinding.begin(),limit=bindings.valuebinding.end();iter!=limit;++iter)
         if (!subBindings[index].valuebinding.count((*iter).first))
            initializations[index].push_back((*iter).second);
   }

   // Build the operator
   Operator* result=new Union(trees,mappings,initializations,plan->cardinality);

   return result;
}
//---------------------------------------------------------------------------
static Operator* translateMergeUnion(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a merge union into an operator tree
{
   // Translate the input
   Binding leftBinding,rightBinding;
   Operator* left=translatePlan(runtime,context,projection,leftBinding,registers,plan->left,pathfilter,ferrari);
   Operator* right=translatePlan(runtime,context,projection,rightBinding,registers,plan->right,pathfilter,ferrari);

   // Collect the binding
   assert(leftBinding.valuebinding.size()==1);
   assert(rightBinding.valuebinding.size()==1);
   unsigned resultVar=(*(leftBinding.valuebinding.begin())).first;
   Register* leftReg=(*(leftBinding.valuebinding.begin())).second,*rightReg=(*(rightBinding.valuebinding.begin())).second;
   bindings.valuebinding[resultVar]=leftReg;

   // Build the operator
   Operator* result=new MergeUnion(leftReg,left,leftReg,right,rightReg,plan->cardinality);

   return result;
}
//---------------------------------------------------------------------------
static Operator* translateTableFunction(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*>& ferrari)
   // Translate a table function into an operator tree
{
   const QueryGraph::TableFunction& function=*reinterpret_cast<QueryGraph::TableFunction*>(plan->right);

   // Build the input trees
   set<unsigned> newProjection=projection;
   for (vector<QueryGraph::TableFunction::Argument>::const_iterator iter=function.input.begin(),limit=function.input.end();iter!=limit;++iter)
      if (~(*iter).id)
         newProjection.insert((*iter).id);
   Operator* tree=translatePlan(runtime,context,newProjection,bindings,registers,plan->left,pathfilter,ferrari);
   vector<TableFunction::FunctionArgument> input;
   for (vector<QueryGraph::TableFunction::Argument>::const_iterator iter=function.input.begin(),limit=function.input.end();iter!=limit;++iter) {
      TableFunction::FunctionArgument arg;
      if (~(*iter).id) {
         arg.reg=bindings.valuebinding[(*iter).id];
      } else {
         arg.reg=0;
         arg.value=(*iter).value;
      }
      input.push_back(arg);
   }

   // Examine output registers
   vector<Register*> output;
   unsigned slot=0;
   for (vector<unsigned>::const_iterator iter=function.output.begin(),limit=function.output.end();iter!=limit;++iter,++slot) {
      Register* reg=runtime.getRegister((*registers.valueregister.find(reinterpret_cast<const QueryGraph::Node*>(&function))).second+slot);
      output.push_back(reg);
      if (projection.count(*iter))
         bindings.valuebinding[*iter]=reg;
   }

   // Build the operator
   Operator* result=new TableFunction(tree,runtime,0 /* XXX */,function.name,input,output,plan->cardinality);

   // Cleanup the binding
   for (vector<QueryGraph::TableFunction::Argument>::const_iterator iter=function.input.begin(),limit=function.input.end();iter!=limit;++iter)
      if ((~(*iter).id)&&(!projection.count((*iter).id)))
         bindings.valuebinding.erase((*iter).id);

   return result;
}
//---------------------------------------------------------------------------
static Operator* translatePlan(Runtime& runtime,const map<unsigned,Register*>& context,const set<unsigned>& projection,Binding& bindings,const MapRegister& registers,Plan* plan,QueryGraph::Filter* pathfilter,map<unsigned,Index*> &ferrari)
   // Translate a plan into an operator tree
{
   Operator* result=0;
   switch (plan->op) {
      case Plan::IndexScan: result=translateIndexScan(runtime,context,projection,bindings,registers,plan,pathfilter); break;
      case Plan::AggregatedIndexScan: result=translateAggregatedIndexScan(runtime,context,projection,bindings,registers,plan,pathfilter); break;
      case Plan::FullyAggregatedIndexScan: result=translateFullyAggregatedIndexScan(runtime,context,projection,bindings,registers,plan,pathfilter); break;
      case Plan::DijkstraScan: result = translateDijkstraScan(runtime,context,projection,bindings,registers,plan,pathfilter); break;
      case Plan::RegularPath: result=translateRegularPathScan(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::NestedLoopJoin: result=translateNestedLoopJoin(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::MergeJoin: result=translateMergeJoin(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::HashJoin: result=translateHashJoin(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::HashGroupify: result=translateHashGroupify(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::Filter: result=translateFilter(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::Union: result=translateUnion(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::MergeUnion: result=translateMergeUnion(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::TableFunction: result=translateTableFunction(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari); break;
      case Plan::Singleton: result=new SingletonScan(); break;
      case Plan::PathFilter: result=translatePathFilter(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari);break;
   }
   return result;
}
//---------------------------------------------------------------------------
static pair<unsigned, unsigned> allocateRegisters(MapRegister& registers,map<unsigned,set<unsigned> >& registerClasses,
									const QueryGraph::SubQuery& query,unsigned id,unsigned pathid)
   // Allocate registers
{
   for (vector<QueryGraph::Node>::const_iterator iter=query.nodes.begin(),limit=query.nodes.end();iter!=limit;++iter) {
      const QueryGraph::Node& node=*iter;
      if (node.pathTriple){
    	  registers.pathregister[&node]=pathid;
    	  pathid++;
      }
      registers.valueregister[&node]=id;

      if (!node.constSubject)
         registerClasses[node.subject].insert(id+0);
      if (!node.constPredicate&&!node.pathTriple)
         registerClasses[node.predicate].insert(id+1);
      if (!node.constObject)
         registerClasses[node.object].insert(id+2);

      id+=3;
   }
   for (vector<QueryGraph::SubQuery>::const_iterator iter=query.optional.begin(),limit=query.optional.end();iter!=limit;++iter){
      pair<unsigned, unsigned> p=allocateRegisters(registers,registerClasses,(*iter),id,pathid);
      id=p.first; pathid=p.second;
   }
   for (vector<vector<QueryGraph::SubQuery> >::const_iterator iter=query.unions.begin(),limit=query.unions.end();iter!=limit;++iter)
      for (vector<QueryGraph::SubQuery>::const_iterator iter2=(*iter).begin(),limit2=(*iter).end();iter2!=limit2;++iter2){
         pair<unsigned, unsigned> p=allocateRegisters(registers,registerClasses,(*iter2),id,pathid);
         id=p.first; pathid=p.second;
      }
   for (vector<QueryGraph::TableFunction>::const_iterator iter=query.tableFunctions.begin(),limit=query.tableFunctions.end();iter!=limit;++iter) {
      registers.valueregister[reinterpret_cast<const QueryGraph::Node*>(&(*iter))]=id;
      unsigned slot=0;
      for (vector<unsigned>::const_iterator iter2=(*iter).output.begin(),limit2=(*iter).output.end();iter2!=limit2;++iter2,++slot)
         registerClasses[*iter2].insert(id+slot);
      id+=(*iter).output.size();
   }
   return pair<unsigned, unsigned>(id, pathid);
}
//---------------------------------------------------------------------------
Operator* CodeGen::translateIntern(Runtime& runtime,const QueryGraph& query,Plan* plan,Output& output,map<unsigned,Index*>& ferrari)
   // Perform a naive translation of a query into an operator tree without output generation
{
   // Allocate registers for all relations
   MapRegister registers;
   map<unsigned,set<unsigned> > registerClasses;

   pair<unsigned,unsigned> p=allocateRegisters(registers,registerClasses,query.getQuery(),0,0);
   unsigned registerCount=p.first;
   unsigned unboundVariable=registerCount;
   unsigned describeVars=query.getQueryForm()==QueryGraph::Describe? unboundVariable+3:unboundVariable;
   runtime.allocateVectorRegisters(p.second);
   runtime.allocateRegisters(describeVars+1);

   // Prepare domain information for join attributes
   {
      // Count the required number of domains
      unsigned domainCount=0;
      for (map<unsigned,set<unsigned> >::const_iterator iter=registerClasses.begin(),limit=registerClasses.end();iter!=limit;++iter) {
         // No join attribute?
         if ((*iter).second.size()<2)
            continue;
         // We have a new domain
         domainCount++;
      }
      runtime.allocateDomainDescriptions(domainCount);

      // And assign registers to domains
      domainCount=0;
      for (map<unsigned,set<unsigned> >::const_iterator iter=registerClasses.begin(),limit=registerClasses.end();iter!=limit;++iter) {
         // No join attribute?
         if ((*iter).second.size()<2)
            continue;
         // Lookup the register addresses
         PotentialDomainDescription* domain=runtime.getDomainDescription(domainCount++);
         for (set<unsigned>::const_iterator iter2=(*iter).second.begin(),limit2=(*iter).second.end();iter2!=limit2;++iter2){
            runtime.getRegister(*iter2)->domain=domain;
         }
      }
   }

   // Build the operator tree
   Operator* tree;
   if (query.knownEmpty()) {
      tree=new EmptyScan();
   } else if (!plan) {
      tree=new SingletonScan();
   } else {
      // Construct the projection
      set<unsigned> projection;
      for (QueryGraph::projection_iterator iter=query.projectionBegin(),limit=query.projectionEnd();iter!=limit;++iter){
         projection.insert(*iter);
      }

      for (QueryGraph::order_iterator iter=query.orderBegin(),limit=query.orderEnd();iter!=limit;++iter)
         if (~(*iter).id)
            projection.insert((*iter).id);

      // And build the tree
      map<unsigned,Register*> context;
      Binding bindings;
      QueryGraph::Filter* pathfilter=0;
      tree=translatePlan(runtime,context,projection,bindings,registers,plan,pathfilter,ferrari);

      // Sort if necessary
      if (query.orderBegin()!=query.orderEnd()) {
         vector<Register*> regs;
         vector<pair<Register*,bool> > order;
         for (set<unsigned>::const_iterator iter=projection.begin(),limit=projection.end();iter!=limit;++iter)
            regs.push_back(bindings.valuebinding[*iter]);
         for (QueryGraph::order_iterator iter=query.orderBegin(),limit=query.orderEnd();iter!=limit;++iter)
            if (~(*iter).id)
               order.push_back(pair<Register*,bool>(bindings.valuebinding[(*iter).id],(*iter).descending)); else
               order.push_back(pair<Register*,bool>(0,(*iter).descending));
         tree=new Sort(runtime.getDatabase(),tree,regs,order,tree->getExpectedOutputCardinality());
      }

      // Remember the output registers
      for (QueryGraph::projection_iterator iter=query.projectionBegin(),limit=query.projectionEnd();iter!=limit;++iter){
         if (bindings.valuebinding.count(*iter)){
         	output.order.push_back(0);
            output.valueoutput.push_back(bindings.valuebinding[*iter]);
         }
         else if (bindings.pathbinding.count(*iter)) {
         	output.order.push_back(1);
         	output.pathoutput.push_back(bindings.pathbinding[*iter]);
         }
         else {
            output.valueoutput.push_back(runtime.getRegister(unboundVariable));
         }
      }
      // prepare registers (s,p,o) for the result of the DESCRIBE operator
      if (query.getQueryForm()==QueryGraph::Describe){
          output.valueoutput.push_back(runtime.getRegister(unboundVariable+1));
          output.valueoutput.push_back(runtime.getRegister(unboundVariable+2));
          output.valueoutput.push_back(runtime.getRegister(unboundVariable+3));
      }
   }

   return tree;
}
//---------------------------------------------------------------------------
Operator* CodeGen::translate(Runtime& runtime,const QueryGraph& query,Plan* plan, map<unsigned,Index*>& ferrari,bool silent)
   // Perform a naive translation of a query into an operator tree
{
   // Build the tree itself
   Output output;

   Operator* tree=translateIntern(runtime,query,plan,output,ferrari);
   if (!tree) return 0;

   // And add the output generation
   ResultsPrinter::DuplicateHandling duplicateHandling=ResultsPrinter::ExpandDuplicates;
   switch (query.getDuplicateHandling()) {
      case QueryGraph::AllDuplicates: duplicateHandling=ResultsPrinter::ExpandDuplicates; break;
      case QueryGraph::CountDuplicates: duplicateHandling=ResultsPrinter::CountDuplicates; break;
      case QueryGraph::ReducedDuplicates: duplicateHandling=ResultsPrinter::ReduceDuplicates; break;
      case QueryGraph::NoDuplicates: duplicateHandling=ResultsPrinter::ReduceDuplicates; break;
      case QueryGraph::ShowDuplicates: duplicateHandling=ResultsPrinter::ShowDuplicates; break;
   }
   // Add new registers, since we want to return all the tuples that contain the URI
   if (query.getQueryForm()==QueryGraph::Describe){
	   Output descrOutput;
	   unsigned regSize=output.valueoutput.size();
	   for (unsigned i=0; i < 3; i++) {
		   descrOutput.valueoutput.push_back(output.valueoutput[regSize-(3-i)]);
		   descrOutput.order.push_back(0);
	   }
	   tree=new DescribeScan(runtime.getDatabase(),tree,output,output.valueoutput[regSize-3],output.valueoutput[regSize-2],output.valueoutput[regSize-1],0);
	   tree=new ResultsPrinter(runtime,tree,descrOutput,duplicateHandling,query.getLimit(),silent);
   } else
	   tree=new ResultsPrinter(runtime,tree,output,duplicateHandling,query.getLimit(),silent);
   return tree;
}
//---------------------------------------------------------------------------
void CodeGen::collectVariables(set<unsigned>& variables,Plan* plan)
   // Collect all variables contained in a plan
{
   ::collectVariables(map<unsigned,Register*>(),variables,plan);
}
//---------------------------------------------------------------------------
