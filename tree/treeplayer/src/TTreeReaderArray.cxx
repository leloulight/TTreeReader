// @(#)root/treeplayer:$Id$
// Author: Axel Naumann, 2011-09-28

/*************************************************************************
 * Copyright (C) 1995-2011, Rene Brun and Fons Rademakers and al.        *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "TTreeReaderArray.h"

#include "TBranchClones.h"
#include "TBranchElement.h"
#include "TBranchRef.h"
#include "TBranchSTL.h"
#include "TBranchProxyDirector.h"
#include "TClassEdit.h"
#include "TLeaf.h"
#include "TROOT.h"
#include "TStreamerInfo.h"
#include "TStreamerElement.h"
#include "TTreeReader.h"

// pin vtable
ROOT::TCollectionReaderABC::~TCollectionReaderABC() {}

namespace {
   // Reader interface for clones arrays
   class TClonesReader: public ROOT::TCollectionReaderABC {
   public:
      ~TClonesReader() {}
      TClonesArray* GetCA(ROOT::TBranchProxy* proxy) {
         return (TClonesArray*) proxy->GetWhere();
      }
      virtual size_t GetSize(ROOT::TBranchProxy* proxy) {
         return GetCA(proxy)->GetEntries();
      }
      virtual void* At(ROOT::TBranchProxy* proxy, size_t idx) {
         return GetCA(proxy)->UncheckedAt(idx);
      }
   };

   // Reader interface for STL
   class TSTLReader: public ROOT::TCollectionReaderABC {
   public:
      ~TSTLReader() {}
      TVirtualCollectionProxy* GetCP(ROOT::TBranchProxy* proxy) {
         return (TVirtualCollectionProxy*) proxy->GetCollection();
      }
      virtual size_t GetSize(ROOT::TBranchProxy* proxy) {
         return GetCP(proxy)->Size();
      }
      virtual void* At(ROOT::TBranchProxy* proxy, size_t idx) {
         return GetCP(proxy)->At(idx);
      }
   };


   // Reader interface for leaf list
   // SEE TTreeProxyGenerator.cxx:1319: '//We have a top level raw type'
   class TLeafListReader: public ROOT::TCollectionReaderABC {
   public:
      ~TLeafListReader() {}
      TVirtualCollectionProxy* GetCP(ROOT::TBranchProxy* proxy) {
         return (TVirtualCollectionProxy*) proxy->GetCollection();
      }
      virtual size_t GetSize(ROOT::TBranchProxy* proxy) {
         return GetCP(proxy)->Size();
      }
      virtual void* At(ROOT::TBranchProxy* proxy, size_t idx) {
         return GetCP(proxy)->At(idx);
      }
   };
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// TTreeReaderArray                                                        //
//                                                                            //
// Extracts array data from a TTree.                                          //
//                                                                            //
//                                                                            //
//                                                                            //
//                                                                            //
//                                                                            //
//                                                                            //
//                                                                            //
//                                                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

ClassImp(TTreeReaderArrayBase)

//______________________________________________________________________________
void ROOT::TTreeReaderArrayBase::CreateProxy()
{
   // Create the proxy object for our branch.
   if (fProxy) {
      Error("CreateProxy()", "Proxy object for branch %s already exists!",
            fBranchName.Data());
      return;
   }
   if (!fTreeReader) {
      Error("CreateProxy()", "TTreeReader object not set / available for branch %s!",
            fBranchName.Data());
      return;
   }
   if (!fDict) {
      TBranch* br = fTreeReader->GetTree()->GetBranch(fBranchName);
      const char* brDataType = "{UNDETERMINED}";
      if (br) {
         TDictionary* dictUnused = 0;
         brDataType = GetBranchDataType(br, dictUnused);
      }
      Error("CreateProxy()", "The template argument type T of %s accessing branch %s (which contains data of type %s) is not known to ROOT. You will need to create a dictionary for it.",
            IsA()->GetName(), fBranchName.Data(), brDataType);
      return;
   }

   // Access a branch's collection content (not the collection itself)
   // through a proxy.
   // Search for the branchname, determine what it contains, and wire the
   // TBranchProxy representing it to us so we can access its data.

   ROOT::TNamedBranchProxy* namedProxy = fTreeReader->FindProxy(fBranchName);
   if (namedProxy && namedProxy->GetContentDict() == fDict) {
      fProxy = namedProxy->GetProxy();
      return;
   }

   TBranch* branch = fTreeReader->GetTree()->GetBranch(fBranchName);
   if (!branch) {
      Error("CreateContentProxy()", "The tree does not have a branch called %s. You could check with TTree::Print() for available branches.", fBranchName.Data());
      fProxy = 0;
      return;
   }

   TDictionary* branchActualType = 0;
   TString branchActualTypeName;
   const char* nonCollTypeName = GetBranchContentDataType(branch, branchActualTypeName, branchActualType);
   if (nonCollTypeName) {
      Error("CreateContentProxy()", "The branch %s contains data of type %s, which should be accessed through a TTreeReaderValue< %s >.",
            fBranchName.Data(), nonCollTypeName, nonCollTypeName);
      fProxy = 0;
      return;
   }
   if (!branchActualType) {
      if (branchActualTypeName.IsNull()) {
         Error("CreateContentProxy()", "Cannot determine the type contained in the collection of branch %s. That's weird - please report!",
               fBranchName.Data());
      } else {
         Error("CreateContentProxy()", "The branch %s contains data of type %s, which does not have a dictionary.",
               fBranchName.Data(), branchActualTypeName.Data());
      }
      fProxy = 0;
      return;
   }

   if (fDict != branchActualType) {
      Error("CreateContentProxy()", "The branch %s contains data of type %s. It cannot be accessed by a TTreeReaderValue<%s>",
            fBranchName.Data(), branchActualType->GetName(), fDict->GetName());

      // Update named proxy's dictionary
      if (namedProxy && !namedProxy->GetContentDict()) {
         namedProxy->SetContentDict(fDict);
      }

      fProxy = 0;
      return;
   }

   // Update named proxy's dictionary
   if (namedProxy && !namedProxy->GetContentDict()) {
      namedProxy->SetContentDict(fDict);
      fProxy = namedProxy->GetProxy();
      return;
   }

   // Access a branch's collection content (not the collection itself)
   // through a proxy.
   // Search for the branchname, determine what it contains, and wire the
   // TBranchProxy representing it to us so we can access its data.
   // A proxy for branch must not have been created before (i.e. check
   // fProxies before calling this function!)

   if (branch->IsA() == TBranchElement::Class()) {
   } else if (branch->IsA() == TBranch::Class()) {
   } else if (branch->IsA() == TBranchClones::Class()) {
   } else if (branch->IsA() == TBranchObject::Class()) {
   } else if (branch->IsA() == TBranchSTL::Class()) {
   } else if (branch->IsA() == TBranchRef::Class()) {
   }

   TString membername;

   bool isTopLevel = branch->GetMother() == branch;
   if (!isTopLevel) {
      membername = strrchr(branch->GetName(), '.');
      if (membername.IsNull()) {
         membername = branch->GetName();
      }
   }
   namedProxy = new ROOT::TNamedBranchProxy(fTreeReader->fDirector, branch, membername);
   fTreeReader->GetProxies()->Add(namedProxy);
   fProxy = namedProxy->GetProxy();
}

//______________________________________________________________________________
const char* ROOT::TTreeReaderArrayBase::GetBranchContentDataType(TBranch* branch,
                                                                 TString& contentTypeName,
                                                                 TDictionary* &dict) const
{
   // Access a branch's collection content (not the collection itself)
   // through a proxy.
   // Retrieve the type of data contained in the collection stored by branch;
   // put its dictionary into dict, If there is no dictionary, put its type
   // name into contentTypeName.
   // The contentTypeName is set to NULL if the branch does not
   // contain a collection; in that case, the type of the branch is returned.
   // In all other cases, NULL is returned.

   dict = 0;
   contentTypeName = "";
   if (branch->IsA() == TBranchElement::Class()) {
      TBranchElement* brElement = (TBranchElement*)branch;
      if (brElement->GetType() == 4
          || brElement->GetType() == 3) {
         TVirtualCollectionProxy* collProxy = brElement->GetCollectionProxy();
         dict = collProxy->GetValueClass();
         if (!dict) dict = TDataType::GetDataType(collProxy->GetType());
         if (!dict) {
            // We don't know the dictionary, thus we need the content's type name.
            // Determine it.
            if (brElement->GetType() == 3) {
               contentTypeName = brElement->GetClonesName();
               return 0;
            }
            // STL:
            TClassEdit::TSplitType splitType(brElement->GetClassName());
            int isSTLCont = splitType.IsSTLCont();
            if (!isSTLCont) {
               Error("GetBranchContentDataType()", "Cannot determine STL collection type of %s stored in branch %s", brElement->GetClassName(), branch->GetName());
               return brElement->GetClassName();
            }
            bool isMap = isSTLCont == TClassEdit::kMap
               || isSTLCont == TClassEdit::kMultiMap;
            if (isMap) contentTypeName = "std::pair< ";
            contentTypeName += splitType.fElements[1];
            if (isMap) {
               contentTypeName += splitType.fElements[2];
               contentTypeName += " >";
            }
            return 0;
         }
         return 0;
      } else if (brElement->GetType() == 31
                 || brElement->GetType() == 41) {
         // it's a member, extract from GetClass()'s streamer info
         TClass* clData = 0;
         EDataType dtData = kOther_t;
         int ExpectedTypeRet = brElement->GetExpectedType(clData, dtData);
         if (ExpectedTypeRet == 0) {
            dict = clData;
            if (!dict) {
               dict = TDataType::GetDataType(dtData);
            }
            if (!dict) {
               Error("GetBranchContentDataType()", "The branch %s contains a data type %d for which the dictionary cannot be retrieved.",
                     branch->GetName(), (int)dtData);
               contentTypeName = TDataType::GetTypeName(dtData);
               return 0;
            }
            return 0;
         } else if (ExpectedTypeRet == 1) {
            int brID = brElement->GetID();
            if (brID == -1) {
               // top
               Error("GetBranchContentDataType()", "The branch %s contains data of type %s for which the dictionary does not exist. It's needed.",
                     branch->GetName(), brElement->GetClassName());
               contentTypeName = brElement->GetClassName();
               return 0;
            }
            // Either the data type name doesn't have an EDataType entry
            // or the streamer info doesn't have a TClass* attached.
            TStreamerElement* element =
               (TStreamerElement*) brElement->GetInfo()->GetElems()[brID];
            contentTypeName = element->GetTypeName();
            return 0;
         }
         /* else (ExpectedTypeRet == 2)*/
         // The streamer info entry cannot be found.
         // TBranchElement::GetExpectedType() has already complained.
         return "{CANNOT DETERMINE TBranchElement DATA TYPE}";
      }
      return 0;
   } else if (branch->IsA() == TBranch::Class()
              || branch->IsA() == TBranchObject::Class()
              || branch->IsA() == TBranchSTL::Class()) {
      const char* dataTypeName = branch->GetClassName();
      if ((!dataTypeName || !dataTypeName[0])
          && branch->IsA() == TBranch::Class()) {
         // leaflist. Can't represent.
         Error("GetBranchContentDataType()", "The branch %s was created using a leaf list and cannot be represented as a C++ type. Please access one of its siblings using a TTreeReaderValueArray:", branch->GetName());
         TIter iLeaves(branch->GetListOfLeaves());
         TLeaf* leaf = 0;
         while ((leaf = (TLeaf*) iLeaves())) {
            Error("GetBranchContentDataType()", "   %s.%s", branch->GetName(), leaf->GetName());
         }
         return 0;
      }
      dict = TDictionary::GetDictionary(dataTypeName);
      return dataTypeName;
   } else if (branch->IsA() == TBranchClones::Class()) {
      dict = TClonesArray::Class();
      return "TClonesArray";
   } else if (branch->IsA() == TBranchRef::Class()) {
      // Can't represent.
      Error("GetBranchContentDataType()", "The branch %s is a TBranchRef and cannot be represented as a C++ type.", branch->GetName());
      return 0;
   } else {
      Error("GetBranchContentDataType()", "The branch %s is of type %s - something that is not handled yet.", branch->GetName(), branch->IsA()->GetName());
      return 0;
   }

   return 0;
}