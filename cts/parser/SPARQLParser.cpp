#include "cts/parser/SPARQLParser.hpp"
#include "cts/parser/SPARQLLexer.hpp"
#include <memory>
#include <cstdlib>
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
SPARQLParser::ParserException::ParserException(const string& message)
  : message(message)
   // Constructor
{
}
//---------------------------------------------------------------------------
SPARQLParser::ParserException::ParserException(const char* message)
  : message(message)
   // Constructor
{
}
//---------------------------------------------------------------------------
SPARQLParser::ParserException::~ParserException()
   // Destructor
{
}
//---------------------------------------------------------------------------
SPARQLParser::Pattern::Pattern(Element subject,Element predicate,Element object)
   : subject(subject),predicate(predicate),object(object)
   // Constructor
{
}
//---------------------------------------------------------------------------
SPARQLParser::Pattern::~Pattern()
   // Destructor
{
}
//---------------------------------------------------------------------------
SPARQLParser::Filter::Filter()
   : arg1(0),arg2(0),arg3(0),valueArg(0)
   // Constructor
{
}
//---------------------------------------------------------------------------
SPARQLParser::Filter::Filter(const Filter& other)
   : type(other.type),arg1(0),arg2(0),arg3(0),value(other.value),valueType(other.valueType),valueArg(other.valueArg)
   // Copy-Constructor
{
   if (other.arg1)
      arg1=new Filter(*other.arg1);
   if (other.arg2)
      arg2=new Filter(*other.arg2);
   if (other.arg3)
      arg3=new Filter(*other.arg3);
}
//---------------------------------------------------------------------------
SPARQLParser::Filter::~Filter()
   // Destructor
{
   delete arg1;
   delete arg2;
   delete arg3;
}
//---------------------------------------------------------------------------
SPARQLParser::Filter& SPARQLParser::Filter::operator=(const Filter& other)
   // Assignment
{
   if (this!=&other) {
      type=other.type;
      delete arg1;
      if (other.arg1)
         arg1=new Filter(*other.arg1); else
         arg1=0;
      delete arg2;
      if (other.arg2)
         arg2=new Filter(*other.arg2); else
         arg2=0;
      delete arg3;
      if (other.arg3)
         arg3=new Filter(*other.arg3); else
         arg3=0;
      value=other.value;
      valueType=other.valueType;
      valueArg=other.valueArg;
   }
   return *this;
}
//---------------------------------------------------------------------------
SPARQLParser::SPARQLParser(SPARQLLexer& lexer)
   : lexer(lexer),variableCount(0),pathVariableCount(0),projectionModifier(Modifier_None),limit(~0u)
   // Constructor
{
}
//---------------------------------------------------------------------------
SPARQLParser::~SPARQLParser()
   // Destructor
{
}
//---------------------------------------------------------------------------
unsigned SPARQLParser::nameVariable(const string& name)
   // Lookup or create a named variable
{
   if (namedVariables.count(name))
      return namedVariables[name];

   unsigned result=variableCount++;
   namedVariables[name]=result;
   return result;
}
//---------------------------------------------------------------------------
void SPARQLParser::parsePrefix()
   // Parse the prefix part if any
{
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();

      if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("prefix"))) {
         // Parse the prefix entry
         if (lexer.getNext()!=SPARQLLexer::Identifier)
            throw ParserException("prefix name expected");
         string name=lexer.getTokenValue();
         if (lexer.getNext()!=SPARQLLexer::Colon)
            throw ParserException("':' expected");
         if (lexer.getNext()!=SPARQLLexer::IRI)
            throw ParserException("IRI expected");
         string iri=lexer.getIRIValue();
         // Register the new prefix
         if (prefixes.count(name))
            throw ParserException("duplicate prefix '"+name+"'");
         prefixes[name]=iri;
      } else {
         lexer.unget(token);
         return;
      }
   }
}
//---------------------------------------------------------------------------
void SPARQLParser::parseProjection()
   // Parse the projection
{
   // Parse the projection
   if ((lexer.getNext()!=SPARQLLexer::Identifier)&&((!lexer.isKeyword("select"))||(!lexer.isKeyword("describe"))))
      throw ParserException("'select' or 'describe' expected");

   if (lexer.isKeyword("select")) queryForm=Select; else
   if (lexer.isKeyword("describe")) queryForm=Describe;

   // Parse modifiers, if any
   {
      SPARQLLexer::Token token=lexer.getNext();
      if (token==SPARQLLexer::Identifier) {
         if (lexer.isKeyword("distinct")) projectionModifier=Modifier_Distinct; else
         if (lexer.isKeyword("reduced")) projectionModifier=Modifier_Reduced; else
         if (lexer.isKeyword("count")) projectionModifier=Modifier_Count; else
         if (lexer.isKeyword("duplicates")) projectionModifier=Modifier_Duplicates; else
            lexer.unget(token);
      } else lexer.unget(token);
   }

   // Parse the projection clause
   bool first=true;
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();
      if (token==SPARQLLexer::Variable) {
         projection.push_back(nameVariable(lexer.getTokenValue()));
      } else if (token == SPARQLLexer::PathVariable){
    	 projection.push_back(nameVariable(lexer.getTokenValue()));
      } else if (token==SPARQLLexer::Mul) {
         // We do nothing here. Empty projections will be filled with all
         // named variables after parsing
      } else {
         if (first)
            throw ParserException("projection required after select");
         lexer.unget(token);
         break;
      }
      first=false;
   }
}
//---------------------------------------------------------------------------
void SPARQLParser::parseFrom()
   // Parse the from part if any
{
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();

      if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("from"))) {
         throw ParserException("from clause currently not implemented");
      } else {
         lexer.unget(token);
         return;
      }
   }
}
//---------------------------------------------------------------------------
void SPARQLParser::parseRDFLiteral(std::string& value,Element::SubType& subType,std::string& valueType)
   // Parse an RDF literal
{
   if (lexer.getNext()!=SPARQLLexer::String)
      throw ParserException("literal expected");
   subType=Element::None;
   value=lexer.getLiteralValue();
   if (value.find('\\')!=string::npos) {
      string v; v.swap(value);
      for (string::const_iterator iter=v.begin(),limit=v.end();iter!=limit;++iter) {
         char c=(*iter);
         if (c=='\\') {
            if ((++iter)==limit) break;
            c=*iter;
         }
         value+=c;
      }
   }

   SPARQLLexer::Token token=lexer.getNext();
   if (token==SPARQLLexer::At) {
      if (lexer.getNext()!=SPARQLLexer::Identifier)
         throw ParserException("language tag expected after '@'");
      subType=Element::CustomLanguage;
      valueType=lexer.getTokenValue();
   } else if (token==SPARQLLexer::Type) {
      token=lexer.getNext();
      if (token==SPARQLLexer::IRI) {
         subType=Element::CustomType;
         valueType=lexer.getIRIValue();
      } else if (token==SPARQLLexer::Identifier){
    	  // prefix::value
          string prefix=lexer.getTokenValue();
          if (lexer.getNext()!=SPARQLLexer::Colon)
             throw ParserException("':' expected after '"+prefix+"'");
          if (!prefixes.count(prefix))
             throw ParserException("unknown prefix '"+prefix+"'");
          if (lexer.getNext()!=SPARQLLexer::Identifier)
             throw ParserException("identifier expected after ':'");
          subType=Element::CustomType;
          valueType=prefixes[prefix]+lexer.getIRIValue();
      } else {
         throw ParserException("type URI expected after '^^'");
      }
   } else {
      lexer.unget(token);
   }
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseIRIrefOrFunction(std::map<std::string,unsigned>& localVars,bool mustCall)
   // Parse a "IRIrefOFunction" production
{
   // The IRI
   if (lexer.getNext()!=SPARQLLexer::IRI)
      throw ParserException("IRI expected");
   unique_ptr<Filter> result(new Filter);
   result->type=Filter::IRI;
   result->value=lexer.getIRIValue();

   // Arguments?
   if (lexer.hasNext(SPARQLLexer::LParen)) {
      lexer.getNext();
      unique_ptr<Filter> call(new Filter);
      call->type=Filter::Function;
      call->arg1=result.release();
      if (lexer.hasNext(SPARQLLexer::RParen)) {
         lexer.getNext();
      } else {
      	unique_ptr<Filter> args(new Filter);
         Filter* tail=args.get();
         tail->type=Filter::ArgumentList;
         tail->arg1=parseExpression(localVars);
         while (true) {
            if (lexer.hasNext(SPARQLLexer::Comma)) {
               lexer.getNext();
               tail=tail->arg2=new Filter;
               tail->type=Filter::ArgumentList;
               tail->arg1=parseExpression(localVars);
            } else {
               if (lexer.getNext()!=SPARQLLexer::RParen)
                  throw ParserException("')' expected");
               break;
            }
         }
         call->arg2=args.release();
      }

      result=std::move(call);
   } else if (mustCall) {
      throw ParserException("'(' expected");
   }

   return result.release();
}
//---------------------------------------------------------------------------
void SPARQLParser::parseContains(unique_ptr<Filter>& result, std::map<std::string,unsigned>& localVars)
// parse two types of 'contains' restriction (containsOnly and containsAny) for paths
{
   if (!lexer.hasNext(SPARQLLexer::LParen)) {
	   throw ParserException("'(' in the path filter expected");
   }
   lexer.getNext();
   if (!lexer.hasNext(SPARQLLexer::PathVariable))
	   throw ParserException("path variable in the path filter expected");
   result->arg1=parsePrimaryExpression(localVars);
   if (!lexer.hasNext(SPARQLLexer::Comma))
	   throw ParserException("',' expected in the path filter");
   lexer.getNext();
   if (lexer.hasNext(SPARQLLexer::IRI)){
	   result->arg2=new Filter();
	   result->arg2->type=Filter::IRI;
	   result->arg2->value=lexer.getIRIValue();
	   lexer.getNext();
   } else
	   throw ParserException("IRI expected in the 'contains' filter");

   if (!lexer.hasNext(SPARQLLexer::RParen))
	   throw ParserException("')' in the path filter expected");
   lexer.getNext();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseBuiltInCall(std::map<std::string,unsigned>& localVars)
   // Parse a "BuiltInCall" production
{
   if (lexer.getNext()!=SPARQLLexer::Identifier)
      throw ParserException("function name expected");

   unique_ptr<Filter> result(new Filter);
   if (lexer.isKeyword("STR")) {
      result->type=Filter::Builtin_str;
      result->arg1=parseBrackettedExpression(localVars);
   } else if (lexer.isKeyword("LANG")) {
      result->type=Filter::Builtin_lang;
      result->arg1=parseBrackettedExpression(localVars);
   } else if (lexer.isKeyword("LANGMATCHES")) {
      result->type=Filter::Builtin_langmatches;
      if (lexer.getNext()!=SPARQLLexer::LParen)
         throw ParserException("'(' expected");
      result->arg1=parseExpression(localVars);
      if (lexer.getNext()!=SPARQLLexer::Comma)
         throw ParserException("',' expected");
      result->arg2=parseExpression(localVars);
      if (lexer.getNext()!=SPARQLLexer::RParen)
         throw ParserException("')' expected");
   } else if (lexer.isKeyword("DATATYPE")) {
      result->type=Filter::Builtin_datatype;
      result->arg1=parseBrackettedExpression(localVars);
   } else if (lexer.isKeyword("BOUND")) {
      result->type=Filter::Builtin_bound;
      if (lexer.getNext()!=SPARQLLexer::LParen)
         throw ParserException("'(' expected");
      if (lexer.getNext()!=SPARQLLexer::Variable)
         throw ParserException("variable expected as argument to BOUND");
      unique_ptr<Filter> arg(new Filter());
      arg->type=Filter::Variable;
      arg->valueArg=nameVariable(lexer.getTokenValue());
      if (lexer.getNext()!=SPARQLLexer::RParen)
         throw ParserException("')' expected");
   } else if (lexer.isKeyword("sameTerm")) {
      result->type=Filter::Builtin_sameterm;
      if (lexer.getNext()!=SPARQLLexer::LParen)
         throw ParserException("'(' expected");
      result->arg1=parseExpression(localVars);
      if (lexer.getNext()!=SPARQLLexer::Comma)
         throw ParserException("',' expected");
      result->arg2=parseExpression(localVars);
      if (lexer.getNext()!=SPARQLLexer::RParen)
         throw ParserException("')' expected");
   } else if (lexer.isKeyword("isIRI")) {
      result->type=Filter::Builtin_isiri;
      result->arg1=parseBrackettedExpression(localVars);
   } else if (lexer.isKeyword("isURI")) {
      result->type=Filter::Builtin_isiri;
      result->arg1=parseBrackettedExpression(localVars);
   } else if (lexer.isKeyword("isBLANK")) {
      result->type=Filter::Builtin_isblank;
      result->arg1=parseBrackettedExpression(localVars);
   } else if (lexer.isKeyword("isLITERAL")) {
      result->type=Filter::Builtin_isliteral;
      result->arg1=parseBrackettedExpression(localVars);
   } else if (lexer.isKeyword("REGEX")) {
      result->type=Filter::Builtin_regex;
      if (lexer.getNext()!=SPARQLLexer::LParen)
         throw ParserException("'(' expected");
      result->arg1=parseExpression(localVars);
      if (lexer.getNext()!=SPARQLLexer::Comma)
         throw ParserException("',' expected");
      result->arg2=parseExpression(localVars);
      if (lexer.hasNext(SPARQLLexer::Comma)) {
         lexer.getNext();
         result->arg3=parseExpression(localVars);
      }
      if (lexer.getNext()!=SPARQLLexer::RParen)
         throw ParserException("')' expected");
   } else if (lexer.isKeyword("in")) {
      result->type=Filter::Builtin_in;
      if (lexer.getNext()!=SPARQLLexer::LParen)
         throw ParserException("'(' expected");
      result->arg1=parseExpression(localVars);

      if (lexer.hasNext(SPARQLLexer::RParen)) {
         lexer.getNext();
      } else {
         if (lexer.getNext()!=SPARQLLexer::Comma)
            throw ParserException("',' expected");
         unique_ptr<Filter> args(new Filter);
         Filter* tail=args.get();
         tail->type=Filter::ArgumentList;
         tail->arg1=parseExpression(localVars);
         while (true) {
            if (lexer.hasNext(SPARQLLexer::Comma)) {
               lexer.getNext();
               tail=tail->arg2=new Filter;
               tail->type=Filter::ArgumentList;
               tail->arg1=parseExpression(localVars);
            } else {
               if (lexer.getNext()!=SPARQLLexer::RParen)
                  throw ParserException("')' expected");
               break;
            }
         }
         result->arg2=args.release();
      }
   } else if (lexer.isKeyword("maxlength")){
	   result->type=Filter::Builtin_length;
	   /// more general version, commented out for now
//	   Filter* tail=parseExpression(localVars);
//	   if (!(tail->type != Filter::Equal || tail->type != Filter::Less || tail->type != Filter::LessOrEqual || tail->type != Filter::Greater || tail->type != Filter::GreaterOrEqual || tail->type != Filter::NotEqual)){
//		   throw ParserException("invalid length comparison in the path filter ");
//	   }
//	   result->arg1 = new Filter();
//	   result->arg1->type=tail->type;
//
//	   result->arg2=tail->arg1;
//	   result->arg3=tail->arg2;
	   if (!lexer.hasNext(SPARQLLexer::LParen)) {
		   throw ParserException("'(' in the path filter expected");
	   }
	   lexer.getNext();
	   if (!lexer.hasNext(SPARQLLexer::PathVariable))
		   throw ParserException("path variable in the path filter expected");
	   result->arg1=parsePrimaryExpression(localVars);
	   if (!lexer.hasNext(SPARQLLexer::Comma))
		   throw ParserException("',' expected in the path filter");
	   lexer.getNext();
	   if (!lexer.hasNext(SPARQLLexer::Integer)){
		   throw ParserException("integer expected in path filter for length");
	   }
	   result->arg2=new Filter();
	   result->arg2->type=Filter::Literal;
	   result->arg2->value=lexer.getLiteralValue();
	   lexer.getNext();
	   if (!lexer.hasNext(SPARQLLexer::RParen))
		   throw ParserException("')' in the path filter expected");
	   lexer.getNext();
   } else if (lexer.isKeyword("containsany")) {
	   parseContains(result,localVars);
	   result->type=Filter::Builtin_containsAny;
   } else if (lexer.isKeyword("containsonly")){
	   parseContains(result,localVars);
	   result->type=Filter::Builtin_containsOnly;
   } else {
	   throw ParserException("unknown function '"+lexer.getTokenValue()+"'");
   }
   return result.release();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parsePrimaryExpression(map<string,unsigned>& localVars)
   // Parse a "PrimaryExpression" production
{
   SPARQLLexer::Token token=lexer.getNext();

   if (token==SPARQLLexer::LParen) {
      lexer.unget(token);
      return parseBrackettedExpression(localVars);
   }
   if (token==SPARQLLexer::Identifier) {
      if (lexer.isKeyword("true")) {
      	unique_ptr<Filter> result(new Filter);
         result->type=Filter::Literal;
         result->value="true";
         result->valueType="http://www.w3.org/2001/XMLSchema#boolean";
         result->valueArg=Element::CustomType;
         return result.release();
      } else if (lexer.isKeyword("false")) {
      	unique_ptr<Filter> result(new Filter);
         result->type=Filter::Literal;
         result->value="false";
         result->valueType="http://www.w3.org/2001/XMLSchema#boolean";
         result->valueArg=Element::CustomType;
         return result.release();
      }
      lexer.unget(token);
      return parseBuiltInCall(localVars);
   }
   if (token==SPARQLLexer::IRI) {
      lexer.unget(token);
      return parseIRIrefOrFunction(localVars,false);
   }
   if (token==SPARQLLexer::String) {
      lexer.unget(token);
      unique_ptr<Filter> result(new Filter);
      result->type=Filter::Literal;
      Element::SubType type;
      parseRDFLiteral(result->value,type,result->valueType);
      result->valueArg=type;
      return result.release();
   }
   if (token==SPARQLLexer::Integer) {
   	unique_ptr<Filter> result(new Filter);
      result->type=Filter::Literal;
      result->value=lexer.getTokenValue();
      result->valueType="http://www.w3.org/2001/XMLSchema#integer";
      result->valueArg=Element::CustomType;
      return result.release();
   }
   if (token==SPARQLLexer::Decimal) {
   	unique_ptr<Filter> result(new Filter);
      result->type=Filter::Literal;
      result->value=lexer.getTokenValue();
      result->valueType="http://www.w3.org/2001/XMLSchema#decimal";
      result->valueArg=Element::CustomType;
      return result.release();
   }
   if (token==SPARQLLexer::Double) {
   	unique_ptr<Filter> result(new Filter);
      result->type=Filter::Literal;
      result->value=lexer.getTokenValue();
      result->valueType="http://www.w3.org/2001/XMLSchema#double";
      result->valueArg=Element::CustomType;
      return result.release();
   }
   if (token==SPARQLLexer::Variable) {
   	unique_ptr<Filter> result(new Filter);
      result->type=Filter::Variable;
      result->value=lexer.getTokenValue();
      result->valueArg=nameVariable(result->value);
      return result.release();
   }
   if (token==SPARQLLexer::PathVariable) {
   	unique_ptr<Filter> result(new Filter);
	   result->type=Filter::PathVariable;
	   result->value=lexer.getTokenValue();
	   result->valueArg=nameVariable(result->value);
	   return result.release();
   }
   throw ParserException("syntax error in primary expression");
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseUnaryExpression(map<string,unsigned>& localVars)
   // Parse a "UnaryExpression" production
{
   SPARQLLexer::Token token=lexer.getNext();

   if (token==SPARQLLexer::Not) {
   	unique_ptr<Filter> result(new Filter);
      result->type=Filter::Not;
      result->arg1=parsePrimaryExpression(localVars);
      return result.release();
   } else if (token==SPARQLLexer::Plus) {
   	unique_ptr<Filter> result(new Filter);
      result->type=Filter::UnaryPlus;
      result->arg1=parsePrimaryExpression(localVars);
      return result.release();
   } else if (token==SPARQLLexer::Minus) {
   	unique_ptr<Filter> result(new Filter);
      result->type=Filter::UnaryMinus;
      result->arg1=parsePrimaryExpression(localVars);
      return result.release();
   } else {
      lexer.unget(token);
      return parsePrimaryExpression(localVars);
   }
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseMultiplicativeExpression(map<string,unsigned>& localVars)
   // Parse a "MultiplicativeExpression" production
{
	unique_ptr<Filter> result(parseUnaryExpression(localVars));

   // op *
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();
      if ((token==SPARQLLexer::Mul)||(token==SPARQLLexer::Div)) {
      	unique_ptr<Filter> right(parseUnaryExpression(localVars));

      	unique_ptr<Filter> newEntry(new Filter);
         switch (token) {
            case SPARQLLexer::Mul: newEntry->type=Filter::Mul; break;
            case SPARQLLexer::Div: newEntry->type=Filter::Div; break;
            default: throw; // cannot happen
         }
         newEntry->arg1=result.release();
         newEntry->arg2=right.release();
         result=std::move(newEntry);
      } else {
         lexer.unget(token);
         break;
      }
   }
   return result.release();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseAdditiveExpression(map<string,unsigned>& localVars)
   // Parse a "AdditiveExpression" production
{
	unique_ptr<Filter> result(parseMultiplicativeExpression(localVars));

   // op *
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();
      if ((token==SPARQLLexer::Plus)||(token==SPARQLLexer::Minus)) {
      	unique_ptr<Filter> right(parseMultiplicativeExpression(localVars));

      	unique_ptr<Filter> newEntry(new Filter);
         switch (token) {
            case SPARQLLexer::Plus: newEntry->type=Filter::Plus; break;
            case SPARQLLexer::Minus: newEntry->type=Filter::Minus; break;
            default: throw; // cannot happen
         }
         newEntry->arg1=result.release();
         newEntry->arg2=right.release();
         result=std::move(newEntry);
      } else {
         lexer.unget(token);
         break;
      }
   }
   return result.release();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseNumericExpression(map<string,unsigned>& localVars)
   // Parse a "NumericExpression" production
{
   return parseAdditiveExpression(localVars);
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseRelationalExpression(map<string,unsigned>& localVars)
   // Parse a "RelationalExpression" production
{
	unique_ptr<Filter> result(parseNumericExpression(localVars));

   // op *
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();
      if ((token==SPARQLLexer::Equal)||(token==SPARQLLexer::NotEqual)||(token==SPARQLLexer::Less)||(token==SPARQLLexer::LessOrEqual)||(token==SPARQLLexer::Greater)||(token==SPARQLLexer::GreaterOrEqual)) {
      	unique_ptr<Filter> right(parseNumericExpression(localVars));

      	unique_ptr<Filter> newEntry(new Filter);
         switch (token) {
            case SPARQLLexer::Equal: newEntry->type=Filter::Equal; break;
            case SPARQLLexer::NotEqual: newEntry->type=Filter::NotEqual; break;
            case SPARQLLexer::Less: newEntry->type=Filter::Less; break;
            case SPARQLLexer::LessOrEqual: newEntry->type=Filter::LessOrEqual; break;
            case SPARQLLexer::Greater: newEntry->type=Filter::Greater; break;
            case SPARQLLexer::GreaterOrEqual: newEntry->type=Filter::GreaterOrEqual; break;
            default: throw; // cannot happen
         }
         newEntry->arg1=result.release();
         newEntry->arg2=right.release();
         result=std::move(newEntry);
      } else {
         lexer.unget(token);
         break;
      }
   }
   return result.release();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseValueLogical(map<string,unsigned>& localVars)
   // Parse a "ValueLogical" production
{
   return parseRelationalExpression(localVars);
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseConditionalAndExpression(map<string,unsigned>& localVars)
   // Parse a "ConditionalAndExpression" production
{
	unique_ptr<Filter> result(parseValueLogical(localVars));

   // && *
   while (lexer.hasNext(SPARQLLexer::And)) {
      if (lexer.getNext()!=SPARQLLexer::And)
         throw ParserException("'&&' expected");
      unique_ptr<Filter> right(parseValueLogical(localVars));

      unique_ptr<Filter> newEntry(new Filter);
      newEntry->type=Filter::And;
      newEntry->arg1=result.release();
      newEntry->arg2=right.release();

      result=std::move(newEntry);
   }
   return result.release();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseConditionalOrExpression(map<string,unsigned>& localVars)
   // Parse a "ConditionalOrExpression" production
{
	unique_ptr<Filter> result(parseConditionalAndExpression(localVars));

   // || *
   while (lexer.hasNext(SPARQLLexer::Or)) {
      if (lexer.getNext()!=SPARQLLexer::Or)
         throw ParserException("'||' expected");
      unique_ptr<Filter> right(parseConditionalAndExpression(localVars));

      unique_ptr<Filter> newEntry(new Filter);
      newEntry->type=Filter::Or;
      newEntry->arg1=result.release();
      newEntry->arg2=right.release();

      result=std::move(newEntry);
   }
   return result.release();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseExpression(map<string,unsigned>& localVars)
   // Parse a "Expression" production
{
   return parseConditionalOrExpression(localVars);
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseBrackettedExpression(map<string,unsigned>& localVars)
   // Parse a "BrackettedExpression" production
{
   // '('
   if (lexer.getNext()!=SPARQLLexer::LParen)
      throw ParserException("'(' expected");

   // Expression
   unique_ptr<Filter> result(parseExpression(localVars));

   // ')'
   if (lexer.getNext()!=SPARQLLexer::RParen)
      throw ParserException("')' expected");

   return result.release();
}
//---------------------------------------------------------------------------
SPARQLParser::Filter* SPARQLParser::parseConstraint(map<string,unsigned>& localVars)
   // Parse a "Constraint" production
{
   // Check possible productions
   if (lexer.hasNext(SPARQLLexer::LParen))
      return parseBrackettedExpression(localVars);
   if (lexer.hasNext(SPARQLLexer::Identifier))
      return parseBuiltInCall(localVars);
   if (lexer.hasNext(SPARQLLexer::IRI))
      return parseIRIrefOrFunction(localVars,true);

   // Report an error
   throw ParserException("filter constraint expected");
}
//---------------------------------------------------------------------------
void SPARQLParser::parseFilter(PatternGroup& group,map<string,unsigned>& localVars)
   // Parse a filter condition
{
   Filter* entry=parseConstraint(localVars);
   group.filters.push_back(*entry);
   delete entry;
}
//---------------------------------------------------------------------------
void SPARQLParser::parsePathFilter(PatternGroup& group,map<string,unsigned>& localVars)
   // Parse a filter condition
{
   Filter* entry=parseConstraint(localVars);
   group.pathfilters.push_back(*entry); 
   delete entry;
}
//---------------------------------------------------------------------------
SPARQLParser::Element SPARQLParser::parseBlankNode(PatternGroup& group,map<string,unsigned>& localVars)
   // Parse blank node patterns
{
   // The subject is a blank node
   Element subject;
   subject.type=Element::Variable;
   subject.id=variableCount++;

   // Parse the the remaining part of the pattern
   SPARQLParser::Element predicate=parsePatternElement(group,localVars);
   SPARQLParser::Element object=parsePatternElement(group,localVars);
   group.patterns.push_back(Pattern(subject,predicate,object));

   // Check for the tail
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();
      if (token==SPARQLLexer::Semicolon) {
         predicate=parsePatternElement(group,localVars);
         object=parsePatternElement(group,localVars);
         group.patterns.push_back(Pattern(subject,predicate,object));
         continue;
      } else if (token==SPARQLLexer::Comma) {
         object=parsePatternElement(group,localVars);
         group.patterns.push_back(Pattern(subject,predicate,object));
         continue;
      } else if (token==SPARQLLexer::Dot) {
         return subject;
      } else if (token==SPARQLLexer::RBracket) {
         lexer.unget(token);
         return subject;
      } else if (token==SPARQLLexer::Identifier) {
         if (!lexer.isKeyword("filter"))
            throw ParserException("'filter' expected");
         parseFilter(group,localVars);
         continue;
      } else {
         // Error while parsing, let out caller handle it
         lexer.unget(token);
         return subject;
      }
   }
}
//---------------------------------------------------------------------------
static bool isRegularPath(SPARQLLexer& lexer){
	return (lexer.hasNext(SPARQLLexer::Mul)
			||lexer.hasNext(SPARQLLexer::Div)
			||lexer.hasNext(SPARQLLexer::Plus));
}
//---------------------------------------------------------------------------
SPARQLParser::Element SPARQLParser::parsePatternElement(PatternGroup& group,map<string,unsigned>& localVars)
   // Parse an entry in a pattern
{
   Element result;
   SPARQLLexer::Token token=lexer.getNext();
   if (token==SPARQLLexer::Variable) {
      result.type=Element::Variable;
      result.id=nameVariable(lexer.getTokenValue());
   } else if (token==SPARQLLexer::PathVariable){
	  result.type=Element::PathVariable;
	  result.id=nameVariable(lexer.getTokenValue());
   } else if (token==SPARQLLexer::String) {
      result.type=Element::Literal;
      lexer.unget(token);
      parseRDFLiteral(result.value,result.subType,result.subTypeValue);
   } else if (token==SPARQLLexer::IRI) {
      result.type=Element::IRI;
      result.value=lexer.getIRIValue();
      if (isRegularPath(lexer)){
         result=parsePropertyPath(result);
      }
   } else if (token==SPARQLLexer::Anon) {
      result.type=Element::Variable;
      result.id=variableCount++;
   } else if (token==SPARQLLexer::LBracket) {
      result=parseBlankNode(group,localVars);
      if (lexer.getNext()!=SPARQLLexer::RBracket)
         throw ParserException("']' expected");
   } else if (token==SPARQLLexer::Underscore) {
      // _:variable
      if (lexer.getNext()!=SPARQLLexer::Colon)
         throw ParserException("':' expected");
      if (lexer.getNext()!=SPARQLLexer::Identifier)
         throw ParserException("identifier expected after ':'");
      result.type=Element::Variable;
      if (localVars.count(lexer.getTokenValue()))
         result.id=localVars[lexer.getTokenValue()]; else
         result.id=localVars[lexer.getTokenValue()]=variableCount++;
   } else if (token==SPARQLLexer::Colon) {
      // :identifier. Should reference the base
      if (lexer.getNext()!=SPARQLLexer::Identifier)
         throw ParserException("identifier expected after ':'");
      result.type=Element::IRI;
      result.value=lexer.getTokenValue();
   } else if (token==SPARQLLexer::Identifier) {
      string prefix=lexer.getTokenValue();
      // Handle the keyword 'a'
      if (prefix=="a") {
      	result.type=Element::IRI;
      	result.value="http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
      } else {
      	// prefix:suffix
      	if (lexer.getNext()!=SPARQLLexer::Colon)
      		throw ParserException("':' expected after '"+prefix+"'");
      	if (!prefixes.count(prefix))
      		throw ParserException("unknown prefix '"+prefix+"'");
      	if (lexer.getNext()!=SPARQLLexer::Identifier)
      		throw ParserException("identifier expected after ':'");
      	result.type=Element::IRI;
      	result.value=prefixes[prefix]+lexer.getIRIValue();
      }
      if (isRegularPath(lexer)){
         result=parsePropertyPath(result);
      }
   } else {
      throw ParserException("invalid pattern element");
   }
   return result;
}
//---------------------------------------------------------------------------
SPARQLParser::Element SPARQLParser::parsePropertyPath(SPARQLParser::Element& start)
	// parse the property path
{
   Element result; Step step;
   result.type=Element::PropertyPath;

   // get the modifier to the starting element
   step.label=start.value;
	SPARQLLexer::Token token=lexer.getNext();
	if (token==SPARQLLexer::Mul){
		step.modifier=Step::Mul;
	} else if (token==SPARQLLexer::Plus){
		step.modifier=Step::Add;
	} else {
		step.modifier=Step::None;
		lexer.unget(token);
	}
	result.path.push_back(step);

	// more labels in the regular path?
	token=lexer.getNext();
	if (token==SPARQLLexer::Div) do{
   	token=lexer.getNext();
   	Step step;
   	// get the edge label
   	if (token==SPARQLLexer::IRI) {
   		step.label=lexer.getIRIValue();
   	} else if (token==SPARQLLexer::Identifier){
      	string prefix=lexer.getTokenValue();
      	//  'a' = type
      	if (prefix=="a") {
      		step.label="http://www.w3.org/1999/02/22-rdf-syntax-ns#type";
      	} else {
      		// prefix:suffix
      		if (lexer.getNext()!=SPARQLLexer::Colon)
      			throw ParserException("':' expected after '"+prefix+"'");
      		if (!prefixes.count(prefix))
      			throw ParserException("unknown prefix '"+prefix+"'");
      		if (lexer.getNext()!=SPARQLLexer::Identifier)
      			throw ParserException("identifier expected after ':'");
      		step.label=prefixes[prefix]+lexer.getIRIValue();
      	}
   	} else
   		throw ParserException("IRI expected in property path");

   	// check modifier
   	token=lexer.getNext();
   	if (token==SPARQLLexer::Mul){
   		step.modifier=Step::Mul;
   	} else if (token==SPARQLLexer::Plus){
   		step.modifier=Step::Add;
   	} else {
   		step.modifier=Step::None;
   		lexer.unget(token);
   	}
   	// add the step to the path
   	result.path.push_back(step);

   	token=lexer.getNext();
	} while (token==SPARQLLexer::Div);

	lexer.unget(token);

   return result;
}
//---------------------------------------------------------------------------
void SPARQLParser::parseGraphPattern(PatternGroup& group)
   // Parse a graph pattern
{
   map<string,unsigned> localVars;

   // Parse the first pattern
   Element subject=parsePatternElement(group,localVars);
   Element predicate=parsePatternElement(group,localVars);
   Element object=parsePatternElement(group,localVars);
   group.patterns.push_back(Pattern(subject,predicate,object));

   if (subject.type == Element::PathVariable)
	   throw ParserException("subject can not contain path expression!");
   if (object.type == Element::PathVariable)
	   throw ParserException("object can not contain path expression!");

   // Check for the tail
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();
      if (token==SPARQLLexer::Semicolon) {
         predicate=parsePatternElement(group,localVars);
         object=parsePatternElement(group,localVars);
         group.patterns.push_back(Pattern(subject,predicate,object));
         continue;
      } else if (token==SPARQLLexer::Comma) {
         object=parsePatternElement(group,localVars);
         group.patterns.push_back(Pattern(subject,predicate,object));
         continue;
      } else if (token==SPARQLLexer::Dot) {
         return;
      } else if (token==SPARQLLexer::RCurly) {
         lexer.unget(token);
         return;
      } else if (token==SPARQLLexer::Identifier) {
         if (!lexer.isKeyword("filter")&&!lexer.isKeyword("pathfilter"))
            throw ParserException("'filter' or 'pathfilter' expected");
         if (lexer.isKeyword("filter"))
        	 parseFilter(group,localVars);
         else
        	 parsePathFilter(group,localVars);
         continue;
      } else {
         // Error while parsing, let our caller handle it
         lexer.unget(token);
         return;
      }
   }
}
//---------------------------------------------------------------------------
void SPARQLParser::parseGroupGraphPattern(PatternGroup& group)
   // Parse a group of patterns
{
   while (true) {
      SPARQLLexer::Token token=lexer.getNext();
      if (token==SPARQLLexer::LCurly) {
         // Parse the group
         PatternGroup newGroup;
         parseGroupGraphPattern(newGroup);

         // Union statement?
         token=lexer.getNext();
         if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("union"))) {
            group.unions.push_back(vector<PatternGroup>());
            vector<PatternGroup>& currentUnion=group.unions.back();
            currentUnion.push_back(newGroup);
            while (true) {
               if (lexer.getNext()!=SPARQLLexer::LCurly)
                  throw ParserException("'{' expected");
               PatternGroup subGroup;
               parseGroupGraphPattern(subGroup);
               currentUnion.push_back(subGroup);

               // Another union?
               token=lexer.getNext();
               if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("union")))
                  continue;
               break;
            }
         } else {
            // No, simply merge it
            group.patterns.insert(group.patterns.end(),newGroup.patterns.begin(),newGroup.patterns.end());
            group.filters.insert(group.filters.end(),newGroup.filters.begin(),newGroup.filters.end());
            group.optional.insert(group.optional.end(),newGroup.optional.begin(),newGroup.optional.end());
            group.unions.insert(group.unions.end(),newGroup.unions.begin(),newGroup.unions.end());
         }
         if (token!=SPARQLLexer::Dot)
            lexer.unget(token);
      } else if ((token==SPARQLLexer::IRI)||(token==SPARQLLexer::Variable)||(token==SPARQLLexer::PathVariable)||(token==SPARQLLexer::Identifier)||(token==SPARQLLexer::String)||(token==SPARQLLexer::Underscore)||(token==SPARQLLexer::Colon)||(token==SPARQLLexer::LBracket)||(token==SPARQLLexer::Anon)) {
         // Distinguish filter conditions
         if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("filter"))) {
            map<string,unsigned> localVars;
            parseFilter(group,localVars);
            token=lexer.getNext();
            if (token!=SPARQLLexer::Dot)
           	     lexer.unget(token);
         } else if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("pathfilter"))) {
        	 map<string,unsigned> localVars;
        	 parsePathFilter(group,localVars);

        	 token=lexer.getNext();
             if (token!=SPARQLLexer::Dot)
            	 lexer.unget(token);
         } else{
            lexer.unget(token);
            parseGraphPattern(group);
         }
      } else if (token==SPARQLLexer::RCurly) {
         break;
      } else {
         throw ParserException("'}' expected");
      }
   }
}
//---------------------------------------------------------------------------
void SPARQLParser::parseWhere()
   // Parse the where part if any
{
   if ((lexer.getNext()!=SPARQLLexer::Identifier)||(!lexer.isKeyword("where")))
      throw ParserException("'where' expected");
   if (lexer.getNext()!=SPARQLLexer::LCurly)
      throw ParserException("'{' expected");

   patterns=PatternGroup();
   parseGroupGraphPattern(patterns);
}
//---------------------------------------------------------------------------
void SPARQLParser::parseOrderBy()
   // Parse the order by part if any
{
   SPARQLLexer::Token token=lexer.getNext();
   if ((token!=SPARQLLexer::Identifier)||(!lexer.isKeyword("order"))) {
      lexer.unget(token);
      return;
   }
   if ((lexer.getNext()!=SPARQLLexer::Identifier)||(!lexer.isKeyword("by")))
      throw ParserException("'by' expected");

   while (true) {
      token=lexer.getNext();
      if (token==SPARQLLexer::Identifier) {
         if (lexer.isKeyword("asc")||lexer.isKeyword("desc")) {
            Order o;
            o.descending=lexer.isKeyword("desc");
            if (lexer.getNext()!=SPARQLLexer::LParen)
               throw ParserException("'(' expected");
            token=lexer.getNext();
            if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("count"))) {
               o.id=~0u;
            } else if (token==SPARQLLexer::Variable) {
               o.id=nameVariable(lexer.getTokenValue());
            } else throw ParserException("variable expected in order-by clause");
            if (lexer.getNext()!=SPARQLLexer::RParen)
               throw ParserException("')' expected");
            order.push_back(o);
         } else if (lexer.isKeyword("count")) {
            Order o; o.id=~0u; o.descending=false;
            order.push_back(o);
         } else {
            lexer.unget(token);
            return;
         }
      } else if (token==SPARQLLexer::Variable) {
         Order o;
         o.id=nameVariable(lexer.getTokenValue());
         o.descending=false;
         order.push_back(o);
      } else if (token==SPARQLLexer::Eof) {
         lexer.unget(token);
         return;
      } else {
         throw ParserException("variable expected in order-by clause");
      }
   }
}
//---------------------------------------------------------------------------
void SPARQLParser::parseLimit()
   // Parse the limit part if any
{
   SPARQLLexer::Token token=lexer.getNext();

   if ((token==SPARQLLexer::Identifier)&&(lexer.isKeyword("limit"))) {
      if (lexer.getNext()!=SPARQLLexer::Integer)
         throw ParserException("number expected after 'limit'");
      limit=atoi(lexer.getTokenValue().c_str());
      if (limit==0)
         throw ParserException("invalid limit specifier");
   } else {
      lexer.unget(token);
   }
}
//---------------------------------------------------------------------------
void SPARQLParser::parse(bool multiQuery)
   // Parse the input
{
   // Parse the prefix part
   parsePrefix();

   // Parse the projection
   parseProjection();

   // Parse the from clause
   parseFrom();

   // Parse the where clause
   parseWhere();

   // Parse the order by clause
   parseOrderBy();

   // Parse the limit clause
   parseLimit();

   // Check that the input is done
   if ((!multiQuery)&&(lexer.getNext()!=SPARQLLexer::Eof))
      throw ParserException("syntax error");

   // Fixup empty projections (i.e. *)
   if (!projection.size()) {
      for (map<string,unsigned>::const_iterator iter=namedVariables.begin(),limit=namedVariables.end();iter!=limit;++iter){
         projection.push_back((*iter).second);
      }
   }

}
//---------------------------------------------------------------------------
string SPARQLParser::getVariableName(unsigned id) const
   // Get the name of a variable
{
   for (map<string,unsigned>::const_iterator iter=namedVariables.begin(),limit=namedVariables.end();iter!=limit;++iter)
      if ((*iter).second==id)
         return (*iter).first;
   return "";
}
//---------------------------------------------------------------------------
