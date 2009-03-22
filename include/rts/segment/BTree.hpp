#ifndef H_rts_segment_BTree
#define H_rts_segment_BTree
//---------------------------------------------------------------------------
#include "rts/segment/Segment.hpp"
#include "rts/buffer/BufferReference.hpp"
#include "rts/database/DatabaseBuilder.hpp"
#include "rts/transaction/LogAction.hpp"
#include <vector>
#include <utility>
#include <cassert>
#include <cstring>
//---------------------------------------------------------------------------
// RDF-3X
// (c) 2009 Thomas Neumann. Web site: http://www.mpi-inf.mpg.de/~neumann/rdf3x
//
// This work is licensed under the Creative Commons
// Attribution-Noncommercial-Share Alike 3.0 Unported License. To view a copy
// of this license, visit http://creativecommons.org/licenses/by-nc-sa/3.0/
// or send a letter to Creative Commons, 171 Second Street, Suite 300,
// San Francisco, California, 94105, USA.
//----------------------------------------------------------------------------
/// A generic B+-tree implementation. Should be used as mix-in
/** Required functionality in the deriving class:
  *
  * /// The size of a key on an inner page. Must be multiple of 4
  * static const unsigned innerKeySize;
  * /// Data type containing an inner key
  * typedef InnerKey;
  * /// Read an inner key
  * static void readInnerKey(InnerKey& key,const unsigned char* ptr);
  * /// Write an inner key
  * static void writeInnerKey(unsigned char* ptr,const InnerKey& key);
  * /// Data type containing a leaf entry
  * typedef LeafEntry;
  * /// Derive an inner key from a leaf entry
  * static InnerKey deriveInnerKey(const LeafEntry& entry);
  * /// Store leaf entries. Returns the number of entries stored
  * static unsigned packLeafEntries(unsigned char* buffer,unsigned char* bufferLimit,vector<LeafEntry>::const_iterator entriesStart,vector<LeafEntry>::const_iterator entriesLimit);
  * /// Restore leaf entries
  * static void unpackLeafEntries(vector<LeafEntry>& entries,const unsigned char* reader,const unsigned char* limit);
  * /// Check for duplicates/conflicts and "merge" if equired
  * static bool mergeConflictWith(const LeafEntry& newEntry,LeafEntry& oldEntry);
  * /// Store info about the leaf pages
  * void updateLeafInfo(unsigned firstLeaf,unsigned leafCount);
  * /// Returns the segment
  * Segment& getSegment();
  * /// Returns the page number of the index root
  * unsigned getRootPage();
  * /// Set the page number of the index root
  * void setRootPage(unsigned pageNo);
  * /// Read a specific page
  * BufferRequest readShared(unsigned page) const;
  * /// Allocate a new page
  * bool allocPage(BufferReferenceModified& page);
  * /// Read a specific page
  * BufferRequestExclusive readExclusive(unsigned page) const;
  */
template <class T> class BTree
{
   private:
   /// Helper for updates
   class Updater;

   /// Get the implementation
   T& impl() { return *static_cast<T*>(this); }
   /// Get the implementation
   const T& impl() const { return *static_cast<const T*>(this); }

   // Layout of an inner page: LSN[8] marker[4] next[4] count[4] padding[4] entries[?)

   /// Size of the header for an inner page
   static const unsigned innerHeaderSize = 24;
   /// Offset of the next pointer
   static const unsigned innerHeaderNextPos = 8+4;
   /// Size of an entry on an inner page
   static const unsigned innerEntrySize = T::innerKeySize+4;
   /// Maximum number of entries on an inner page
   static const unsigned maxInnerCount = (BufferReference::pageSize-innerHeaderSize)/T::innerEntrySize;

   /// Is a page an inner page?
   static bool isInnerPage(const unsigned char* page) { return Segment::readUint32Aligned(page+8)==0xFFFFFFFF; }
   /// Get the number of entries on an inner page
   static inline unsigned getInnerCount(const unsigned char* page) { return Segment::readUint32Aligned(page+16); }
   /// Get the pointer to an inner entry
   static inline const unsigned char* getInnerPtr(const unsigned char* page,unsigned slot) { return (page+innerHeaderSize)+(slot*innerEntrySize); }
   /// Get the page number containing to an inner entry
   static inline unsigned getInnerChildPage(const unsigned char* page,unsigned slot) { return Segment::readUint32Aligned(getInnerPtr(page,slot)+T::innerKeySize); }

   /// Layout of a leaf page: LSN[8] next[4] entries[?]

   /// Size of the header for a leaf page
   static const unsigned leafHeaderSize = 12;
   /// Offset of the next pointer
   static const unsigned leafHeaderNextPos = 8;
   /// Maximum number of entries on an inner page (upper bound)
   static const unsigned maxLeafCount = BufferReference::pageSize;

   /// Create a new level of inner nodes
   template <class V> void packInner(const std::vector<std::pair<V,unsigned> >& data,std::vector<std::pair<V,unsigned> >& boundaries);
   /// Pack the leaf pages
   template <class S,class V> void packLeaves(S& reader,std::vector<std::pair<V,unsigned> >& boundaries);

   public:
   /// Navigate to a leaf node
   template <class V> bool findLeaf(BufferReference& leaf,const V& key);
   /// Perform an initial bulkload
   template <class S> void performBulkload(S& source);
   /// Perform an update
   template <class S> void performUpdate(S& source);
};
//----------------------------------------------------------------------------
template <class T> template <class V> bool BTree<T>::findLeaf(BufferReference& ref,const V& key)
   /// Navigate to a leaf node
{
   // Traverse the B-Tree
   ref=impl().readShared(impl().getRootPage());
   while (true) {
      const unsigned char* page=static_cast<const unsigned char*>(ref.getPage());
      // Inner node?
      if (isInnerPage(page)) {
         // Perform a binary search. The test is more complex as we only have the upper bound for ranges
         unsigned left=0,right=getInnerCount(page);
         while (left!=right) {
            unsigned middle=(left+right)/2;
            typename T::InnerKey innerKey;
            T::readInnerKey(innerKey,getInnerPtr(page,middle));
            if (innerKey<key) {
               left=middle+1;
            } else if (!middle) {
               T::readInnerKey(innerKey,getInnerPtr(page,middle-1));
               if (innerKey<key) {
                  ref=impl().readShared(getInnerChildPage(page,middle));
                  break;
               } else {
                  right=middle;
               }
            } else {
               right=middle;
            }
         }
         // Unsuccessful search?
         if (left==right) {
            ref.reset();
            return false;
         }
      } else {
         // A leaf node, stop here
         return true;
      }
   }
}
//---------------------------------------------------------------------------
template <class T> template <class S,class V> void BTree<T>::packLeaves(S& reader,std::vector<std::pair<V,unsigned> >& boundaries)
   // Pack the leaf pages
{
   DatabaseBuilder::PageChainer chainer(leafHeaderNextPos);
   unsigned char buffer[BufferReference::pageSize];

   // Collect some input
   std::vector<typename T::LeafEntry> entries;
   bool inputDone=false;
   while ((entries.size()<maxLeafCount)&&(!inputDone)) {
      typename T::LeafEntry entry;
      if (!reader.next(entry)) {
         inputDone=true;
         break;
      }
      entries.push_back(entry);
   }

   // Build pages
   while (!entries.empty()) {
      // Store as much as possible
      unsigned stored=T::packLeafEntries(buffer+leafHeaderSize,buffer+sizeof(buffer),entries.begin(),entries.end());
      assert(stored);

      // And write the page
      chainer.store(&impl().getSegment(),buffer);
      boundaries.push_back(std::pair<V,unsigned>(T::deriveInnerKey(entries[stored-1]),chainer.getPageNo()));
      entries.erase(entries.begin(),entries.begin()+stored);

      // Collect more entries
      while ((entries.size()<maxLeafCount)&&(!inputDone)) {
         typename T::LeafEntry entry;
         if (!reader.next(entry)) {
            inputDone=true;
            break;
         }
         entries.push_back(entry);
      }
   }

   chainer.finish();

   impl().updateLeafInfo(chainer.getFirstPageNo(),chainer.getPages());
}
//---------------------------------------------------------------------------
template <class T> template <class V> void BTree<T>::packInner(const std::vector<std::pair<V,unsigned> >& data,std::vector<std::pair<V,unsigned> >& boundaries)
   // Create a new level of inner nodes
{
   boundaries.clear();

   DatabaseBuilder::PageChainer chainer(innerHeaderNextPos);
   unsigned char buffer[BufferReference::pageSize];
   unsigned bufferPos=innerHeaderSize,bufferCount=0;

   for (typename std::vector<std::pair<typename T::InnerKey,unsigned> >::const_iterator iter=data.begin(),limit=data.end();iter!=limit;++iter) {
      // Do we have to start a new page?
      if ((bufferPos+innerEntrySize)>BufferReference::pageSize) {
         for (unsigned index=0;index<8;index++)
            buffer[index]=0;
         Segment::writeUint32Aligned(buffer+8,0xFFFFFFFF);
         Segment::writeUint32Aligned(buffer+12,0);
         Segment::writeUint32Aligned(buffer+16,bufferCount);
         Segment::writeUint32Aligned(buffer+20,0);
         for (unsigned index=bufferPos;index<BufferReference::pageSize;index++)
            buffer[index]=0;
         chainer.store(&impl().getSegment(),buffer);
         boundaries.push_back(std::pair<typename T::InnerKey,unsigned>((*(iter-1)).first,chainer.getPageNo()));
         bufferPos=innerHeaderSize; bufferCount=0;
      }
      // Write the entry
      T::writeInnerKey(buffer+bufferPos,(*iter).first);
      bufferPos+=T::innerKeySize;
      Segment::writeUint32Aligned(buffer+bufferPos,(*iter).second); bufferPos+=4;
      bufferCount++;
   }
   // Write the least page
   for (unsigned index=0;index<8;index++)
      buffer[index]=0;
   Segment::writeUint32Aligned(buffer+8,0xFFFFFFFF);
   Segment::writeUint32Aligned(buffer+12,0);
   Segment::writeUint32Aligned(buffer+16,bufferCount);
   Segment::writeUint32Aligned(buffer+20,0);
   for (unsigned index=bufferPos;index<BufferReference::pageSize;index++)
      buffer[index]=0;
   chainer.store(&impl().getSegment(),buffer);
   boundaries.push_back(std::pair<typename T::InnerKey,unsigned>(data.back().first,chainer.getPageNo()));
   chainer.finish();
}
//---------------------------------------------------------------------------
template <class T> template <class S> void BTree<T>::performBulkload(S& source)
   // Perform an initial bulkload
{
   // Write the leaf pages
   std::vector<std::pair<typename T::InnerKey,unsigned> > boundaries;
   packLeaves(source,boundaries);

   // Only one leaf node? Special case this
   if (boundaries.size()==1) {
      std::vector<std::pair<typename T::InnerKey,unsigned> > newBoundaries;
      packInner(boundaries,newBoundaries);
      std::swap(boundaries,newBoundaries);
   } else {
      // Write the inner nodes
      while (boundaries.size()>1) {
         std::vector<std::pair<typename T::InnerKey,unsigned> > newBoundaries;
         packInner(boundaries,newBoundaries);
         std::swap(boundaries,newBoundaries);
      }
   }

   // Remember the index root
   impl().setRootPage(boundaries.back().second);
}
//---------------------------------------------------------------------------
/// Namespace with log actin helpers
namespace BTreeLogActions {
   class BTree {
      public:
      /// The (pseudo) segment id
      static const unsigned ID = Segment::Type_BTree;
      /// Known actions
      enum Action { Action_UpdateInnerPage, Action_UpdateInner, Action_InsertInner, Action_UpdateLeaf };
   };
   LOGACTION2DECL(BTree,UpdateInnerPage,LogData,oldEntry,LogData,newEntry);
   LOGACTION3DECL(BTree,UpdateInner,uint32_t,slot,LogData,oldEntry,LogData,newEntry);
   LOGACTION2DECL(BTree,InsertInner,uint32_t,slot,LogData,newEntry);
   LOGACTION2DECL(BTree,UpdateLeaf,LogData,oldContent,LogData,newContent);
};
//---------------------------------------------------------------------------
/// Helper for updates
template <class T> class BTree<T>::Updater {
   private:
   /// Maximum tree depth
   static const unsigned maxDepth = 10;

   /// The tree
   BTree<T>& tree;
   /// The pages
   BufferReferenceExclusive pages[maxDepth];
   /// Parent positions
   unsigned positions[maxDepth];
   /// The depth
   unsigned depth;
   /// Do we manipulate the first page after a lookup?
   bool firstPage;

   /// Update a parent entry
   void updateKey(unsigned level,typename T::InnerKey maxKey);

   public:
   /// Constructor
   Updater(BTree<T>& tree);
   /// Destructor
   ~Updater();

   /// Lookup the matching page for a value
   void lookup(const typename T::InnerKey& value);
   /// Store a new or updated leaf page
   void storePage(unsigned char* data,const typename T::InnerKey& maxKey);
   /// Is there a leaf after the current one?
   bool hasNextLeaf();
   /// Get the first value of the next leaf page
   void getNextLeafStart(typename T::InnerKey& key);
   /// Data on the current leaf page
   const unsigned char* getLeafPage();
};
//---------------------------------------------------------------------------
template <class T> BTree<T>::Updater::Updater(BTree<T>& tree)
   // Constructor
   : tree(tree),depth(0)
{
}
//---------------------------------------------------------------------------
template <class T> BTree<T>::Updater::~Updater()
   // Destructor
{
}
//---------------------------------------------------------------------------
template <class T> void BTree<T>::Updater::lookup(const typename T::InnerKey& key)
   // Lookup the matching page for a value
{
   // Release existing pages
   while (depth)
      pages[--depth].reset();

   // Traverse the B-Tree
   pages[0]=tree.impl().readExclusive(tree.impl().getRootPage());
   positions[0]=0;
   depth=1;
   while (true) {
      const unsigned char* page=static_cast<const unsigned char*>(pages[depth-1].getPage());
      // Inner node?
      if (Segment::readUint32Aligned(page+leafHeaderNextPos)==0xFFFFFFFF) {
         // Perform a binary search. The test is more complex as we only have the upper bound for ranges
         unsigned left=0,right=getInnerCount(page),total=right;
         while (left!=right) {
            unsigned middle=(left+right)/2;
            typename T::InnerKey innerKey;
            T::readInnerKey(innerKey,getInnerPtr(page,middle));
            if (innerKey<key) {
               left=middle+1;
            } else if (!middle) {
               T::readInnerKey(innerKey,getInnerPtr(page,middle-1));
               if (innerKey<key) {
                  left=middle;
                  break;
               } else {
                  right=middle;
               }
            } else {
               right=middle;
            }
         }
         // Unsuccessful search? Then pick the right-most entry
         if (left==total)
            left=total-1;

         // Go down
         pages[depth]=tree.impl().readExclusive(getInnerChildPage(page,left));
         positions[depth]=left;
         ++depth;
      } else {
         // We reached a leaf
         firstPage=true;
         return;
      }
   }
}
//---------------------------------------------------------------------------
template <class T> void BTree<T>::Updater::updateKey(unsigned level,typename T::InnerKey maxKey)
   // Update a parent entry
{
   // Update the parent
   BufferReferenceModified parent;
   parent.modify(pages[level]);
   unsigned char* parentData=static_cast<unsigned char*>(parent.getPage());
   unsigned char newEntry[innerEntrySize];
   T::writeInnerKey(newEntry,maxKey);
   Segment::writeUint32Aligned(newEntry+T::innerKeySize,getInnerChildPage(parentData,positions[level+1]));
   BTreeLogActions::UpdateInner(positions[level+1],LogData(getInnerPtr(parentData,positions[level+1]),innerEntrySize),LogData(newEntry,innerEntrySize)).applyButKeep(parent,pages[level]);

   // Update further up if necessary
   for (;level>0;--level) {
      // Not the maximum?
      if (positions[level]!=getInnerCount(static_cast<const unsigned char*>(pages[level-1].getPage())))
         break;
      // Modify the parent
      parent.modify(pages[level-1]);
      parentData=static_cast<unsigned char*>(parent.getPage());
      Segment::writeUint32Aligned(newEntry+T::innerKeySize,getInnerChildPage(parentData,positions[level]));
      BTreeLogActions::UpdateInner(positions[level],LogData(getInnerPtr(parentData,positions[level]),innerEntrySize),LogData(newEntry,innerEntrySize)).applyButKeep(parent,pages[level-1]);
   }
}
//---------------------------------------------------------------------------
template <class T> void BTree<T>::Updater::storePage(unsigned char* data,const typename T::InnerKey& maxKey)
   // Store a new or updated leaf page
{
   if (firstPage) {
      // Update the page itself
      BufferReferenceModified leaf;
      leaf.modify(pages[depth-1]);
      unsigned char* leafData=static_cast<unsigned char*>(leaf.getPage());
      std::memcpy(data+8,leafData+8,4); // copy the next pointer
      BTreeLogActions::UpdateLeaf(LogData(leafData+8,BufferReference::pageSize-8),LogData(data+8,BufferReference::pageSize-8)).applyButKeep(leaf,pages[depth-1]);

      // Update the parent
      updateKey(depth-2,maxKey);
   } else {
      // Allocate a new page
      BufferReferenceModified newLeaf;
      tree.impl().allocPage(newLeaf);
      unsigned newLeafNo=newLeaf.getPageNo();
      unsigned char oldNext[4],newNext[4];
      Segment::writeUint32Aligned(newNext,newLeafNo);
      memcpy(oldNext,static_cast<const unsigned char*>(pages[depth-1].getPage())+8,4);
      memcpy(data+8,oldNext,4);

      // Update the old page next pointer
      BufferReferenceModified oldLeaf;
      oldLeaf.modify(pages[depth-1]);
      BTreeLogActions::UpdateLeaf(LogData(oldNext,4),LogData(newNext,4)).apply(oldLeaf);

      // And write the new page
      BTreeLogActions::UpdateLeaf(LogData(static_cast<unsigned char*>(newLeaf.getPage())+8,BufferReference::pageSize-8),LogData(data+8,BufferReference::pageSize-8)).applyButKeep(newLeaf,pages[depth-1]);

      // Insert in parent
      typename T::InnerKey insertKey=maxKey;
      unsigned insertPage=pages[depth-1].getPageNo();
      bool insertRight=true;
      for (unsigned level=depth-2;;--level) {
         // Fits?
         if (getInnerCount(static_cast<const unsigned char*>(pages[level].getPage()))<maxInnerCount) {
            BufferReferenceModified inner;
            inner.modify(pages[level]);
            unsigned char newEntry[innerEntrySize];
            T::writeInnerKey(newEntry,insertKey);
            Segment::writeUint32Aligned(newEntry+T::innerKeySize,insertPage);
            BTreeLogActions::InsertInner(positions[level+1]+1,LogData(newEntry,innerEntrySize));
            if ((positions[level+1]+1==getInnerCount(static_cast<const unsigned char*>(pages[level].getPage())))&&(level>0))
               updateKey(level-1,insertKey);
            if (insertRight)
               positions[level+1]++;
            break;
         }
         // No, we have to split
         BufferReferenceModified newInner;
         tree.impl().allocPage(newInner);
         unsigned char leftPage[BufferReference::pageSize],rightPage[BufferReference::pageSize];
         memset(leftPage,0,BufferReference::pageSize);
         Segment::writeUint32Aligned(leftPage+8,~0u);
         Segment::writeUint32Aligned(leftPage+12,newInner.getPageNo());
         Segment::writeUint32Aligned(leftPage+16,maxInnerCount/2);
         memcpy(leftPage+innerHeaderSize,static_cast<const unsigned char*>(pages[level].getPage())+innerHeaderSize,innerEntrySize*(maxInnerCount/2));
         memset(rightPage,0,BufferReference::pageSize);
         Segment::writeUint32Aligned(rightPage+8,~0u);
         Segment::writeUint32Aligned(leftPage+12,Segment::readUint32Aligned(static_cast<const unsigned char*>(pages[level].getPage())+12));
         Segment::writeUint32Aligned(leftPage+16,maxInnerCount-(maxInnerCount/2));
         memcpy(rightPage+innerHeaderSize,static_cast<const unsigned char*>(pages[level].getPage())+innerHeaderSize+innerEntrySize*((maxInnerCount/2)),innerEntrySize*(maxInnerCount-(maxInnerCount/2)));
         typename T::InnerKey leftMax;
         T::readInnerKey(leftMax,leftPage+innerHeaderSize+innerEntrySize*((maxInnerCount/2)-1));
         typename T::InnerKey rightMax;
         T::readInnerKey(rightMax,leftPage+innerHeaderSize+innerEntrySize*(maxInnerCount-(maxInnerCount/2)-1));
         if (level>0)
            updateKey(level-1,leftMax);

         // Update the entries
         BufferReferenceModified inner;
         inner.modify(pages[level]);
         insertKey=rightMax; insertPage=newInner.getPageNo();
         unsigned leftPageNo=inner.getPageNo();
         if ((insertKey<leftMax)||(insertKey==leftMax)) {
            BTreeLogActions::UpdateInnerPage(LogData(static_cast<unsigned char*>(newInner.getPage())+8,BufferReference::pageSize-8),LogData(rightPage+8,BufferReference::pageSize-8)).apply(newInner);
            BTreeLogActions::UpdateInnerPage(LogData(static_cast<unsigned char*>(inner.getPage())+8,BufferReference::pageSize-8),LogData(leftPage+8,BufferReference::pageSize-8)).applyButKeep(inner,pages[level]);
            insertRight=false;
         } else {
            BTreeLogActions::UpdateInnerPage(LogData(static_cast<unsigned char*>(inner.getPage())+8,BufferReference::pageSize-8),LogData(leftPage+8,BufferReference::pageSize-8)).apply(inner);
            BTreeLogActions::UpdateInnerPage(LogData(static_cast<unsigned char*>(newInner.getPage())+8,BufferReference::pageSize-8),LogData(rightPage+8,BufferReference::pageSize-8)).applyButKeep(newInner,pages[level]);
            positions[level+1]-=maxInnerCount/2;
            insertRight=true;
         }

         // Do we need a new root?
         if (!level) {
            for (unsigned index=depth;index>0;index--) {
               pages[index].swap(pages[index-1]);
               positions[index]=positions[index-1];
            }
            ++depth;
            BufferReferenceModified newRoot;
            tree.impl().allocPage(newRoot);
            unsigned char newPage[BufferReference::pageSize];
            Segment::writeUint32(newPage+8,~0u);
            Segment::writeUint32(newPage+12,0);
            Segment::writeUint32(newPage+16,2);
            Segment::writeUint32(newPage+20,0);
            T::writeInnerKey(newPage+innerHeaderSize,leftMax);
            Segment::writeUint32(newPage+innerHeaderSize+T::innerKeySize,leftPageNo);
            T::writeInnerKey(newPage+innerHeaderSize+innerEntrySize,rightMax);
            Segment::writeUint32(newPage+innerHeaderSize+innerEntrySize+T::innerKeySize,insertPage);
            BTreeLogActions::UpdateInnerPage(LogData(static_cast<unsigned char*>(newRoot.getPage())+8,BufferReference::pageSize-8),LogData(newPage+8,BufferReference::pageSize-8)).applyButKeep(newRoot,pages[0]);
            tree.impl().setRootPage(pages[0].getPageNo());
            break;
         }
      }
   }
}
//---------------------------------------------------------------------------
template <class T> bool BTree<T>::Updater::hasNextLeaf()
   // Is there a leaf after the current one?
{
   return Segment::readUint32Aligned(getLeafPage()+leafHeaderNextPos)!=0;
}
//---------------------------------------------------------------------------
template <class T> void BTree<T>::Updater::getNextLeafStart(typename T::InnerKey& key)
   // Get the first value of the next leaf page
{
   unsigned nextPageNo=Segment::readUint32Aligned(getLeafPage()+leafHeaderNextPos);
   BufferReference page(tree.impl().readShared(nextPageNo));
   T::readFirstLeafEntryKey(key,static_cast<const unsigned char*>(page.getPage())+leafHeaderSize);
}
//---------------------------------------------------------------------------
template <class T> const unsigned char* BTree<T>::Updater::getLeafPage()
   // Data on the current leaf page
{
   return static_cast<const unsigned char*>(pages[depth-1].getPage());
}
//---------------------------------------------------------------------------
template <class T> template <class S> void BTree<T>::performUpdate(S& source)
   // Load new data
{
   // Examine the input
   typename T::LeafEntry current;
   if (!source.next(current))
      return;
   bool hasCurrent=true,sourceDone=false;

   Updater updater(*this);
   unsigned char buffer[BufferReference::pageSize];
   // Load the input
   while (true) {
      // Examine the next input if necessary
      if (!hasCurrent) {
         if (sourceDone||(!source.next(current)))
            break;
         hasCurrent=true;
      }

      // Access the current page and find the merge limit
      updater.lookup(T::deriveInnerKey(current));
      typename T::InnerKey mergeLimit;
      bool hasMergeLimit=false;
      if (updater.hasNextLeaf()) {
         updater.getNextLeafStart(mergeLimit);
         hasMergeLimit=true;
      }

      // Read the current page
      std::vector<typename T::LeafEntry> currentEntries;
      T::unpackLeafEntries(currentEntries,updater.getLeafPage()+leafHeaderSize,updater.getLeafPage()+BufferReference::pageSize);
      typename std::vector<typename T::LeafEntry>::const_iterator currentEntriesIter=currentEntries.begin(),currentEntriesLimit=currentEntries.end();

      // Merge and store
      std::vector<typename T::LeafEntry> mergedEntries;
      while (true) {
         // Merge more triples if necessary
         while (mergedEntries.size()<T::maxLeafCount) {
            // Make sure we have data if possible
            if ((!hasCurrent)&&(!sourceDone)) {
               if (!source.next(current))
                  sourceDone=true; else
                  hasCurrent=true;
            }
            // Current page done?
            if (currentEntriesIter==currentEntriesLimit) {
               if (sourceDone)
                  break;
               if ((hasMergeLimit)&&(!(current<mergeLimit)))
                  break;
               mergedEntries.push_back(current);
               hasCurrent=false;
               continue;
            }
            // Input done?
            if (sourceDone||((hasMergeLimit)&&(!(current<mergeLimit)))) {
               mergedEntries.push_back(*(currentEntriesIter++));
               continue;
            }
            // Compare
            if (current<(*currentEntriesIter)) {
               // Check if we can merge it
               if ((!mergedEntries.empty())&&(T::mergeConflictWith(current,mergedEntries.back()))) {
                  source.markAsConflict();
               } else {
                  mergedEntries.push_back(current);
               }
               hasCurrent=false;
            } else if ((*currentEntriesIter)<current) {
               mergedEntries.push_back(*(currentEntriesIter++));
            } else {
               // We found a duplicate, ignore the added value
               mergedEntries.push_back(*(currentEntriesIter++));
               source.markAsConflict();
               hasCurrent=false;
            }
         }
         if (mergedEntries.empty())
            break;

         // Pack the merged entries on the page
         unsigned stored=T::packLeafEntries(buffer+leafHeaderSize,buffer+sizeof(buffer),mergedEntries.begin(),mergedEntries.end());
         assert(stored);

         // Make sure we did not chop a run into parts
         if ((stored<mergedEntries.size())&&(T::deriveInnerKey(mergedEntries[stored-1])==T::deriveInnerKey(mergedEntries[stored-1]))) {
            while (stored&&(T::deriveInnerKey(mergedEntries[stored-1])==T::deriveInnerKey(mergedEntries[stored-1])))
               --stored;
            assert(stored);
            unsigned s=T::packLeafEntries(buffer+leafHeaderSize,buffer+sizeof(buffer),mergedEntries.begin(),mergedEntries.begin()+stored);
            assert(stored==s);
         }

         // And write the page
         updater.storePage(buffer,T::deriveInnerKey(mergedEntries[stored-1]));
         mergedEntries.erase(mergedEntries.begin(),mergedEntries.begin()+stored);
      }
   }
}
//----------------------------------------------------------------------------
#endif
