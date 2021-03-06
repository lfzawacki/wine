/*
 * Implementation of Shaping for the Uniscribe Script Processor (usp10.dll)
 *
 * Copyright 2010 CodeWeavers, Aric Stewart
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "usp10.h"
#include "winternl.h"

#include "usp10_internal.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(uniscribe);

#define FIRST_ARABIC_CHAR 0x0600
#define LAST_ARABIC_CHAR  0x06ff

typedef VOID (*ContextualShapingProc)(HDC, ScriptCache*, SCRIPT_ANALYSIS*,
                                      WCHAR*, INT, WORD*, INT*, INT, WORD*);

static void ContextualShape_Arabic(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WCHAR* pwcChars, INT cChars, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, WORD *pwLogClust);
static void ContextualShape_Syriac(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WCHAR* pwcChars, INT cChars, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, WORD *pwLogClust);
static void ContextualShape_Phags_pa(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WCHAR* pwcChars, INT cChars, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, WORD *pwLogClust);


typedef VOID (*ShapeCharGlyphPropProc)( HDC , ScriptCache*, SCRIPT_ANALYSIS*, const WCHAR*, const INT, const WORD*, const INT, WORD*, SCRIPT_CHARPROP*, SCRIPT_GLYPHPROP*);

static void ShapeCharGlyphProp_Default( HDC hdc, ScriptCache* psc, SCRIPT_ANALYSIS* psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD* pwLogClust, SCRIPT_CHARPROP* pCharProp, SCRIPT_GLYPHPROP* pGlyphProp);
static void ShapeCharGlyphProp_Arabic( HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD *pwLogClust, SCRIPT_CHARPROP* pCharProp, SCRIPT_GLYPHPROP *pGlyphProp );
static void ShapeCharGlyphProp_Thai( HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD *pwLogClust, SCRIPT_CHARPROP *pCharProp, SCRIPT_GLYPHPROP *pGlyphProp );
static void ShapeCharGlyphProp_None( HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD *pwLogClust, SCRIPT_CHARPROP *pCharProp, SCRIPT_GLYPHPROP *pGlyphProp );

extern const unsigned short wine_shaping_table[];
extern const unsigned short wine_shaping_forms[LAST_ARABIC_CHAR - FIRST_ARABIC_CHAR + 1][4];

enum joining_types {
    jtU,
    jtT,
    jtR,
    jtL,
    jtD,
    jtC
};

enum joined_forms {
    Xn=0,
    Xr,
    Xl,
    Xm,
    /* Syriac Alaph */
    Afj,
    Afn,
    Afx
};

#ifdef WORDS_BIGENDIAN
#define GET_BE_WORD(x) (x)
#else
#define GET_BE_WORD(x) RtlUshortByteSwap(x)
#endif

/* These are all structures needed for the GSUB table */
#define GSUB_TAG MS_MAKE_TAG('G', 'S', 'U', 'B')
#define GSUB_E_NOFEATURE -2
#define GSUB_E_NOGLYPH -1

typedef struct {
    DWORD version;
    WORD ScriptList;
    WORD FeatureList;
    WORD LookupList;
} GSUB_Header;

typedef struct {
    CHAR ScriptTag[4];
    WORD Script;
} GSUB_ScriptRecord;

typedef struct {
    WORD ScriptCount;
    GSUB_ScriptRecord ScriptRecord[1];
} GSUB_ScriptList;

typedef struct {
    CHAR LangSysTag[4];
    WORD LangSys;
} GSUB_LangSysRecord;

typedef struct {
    WORD DefaultLangSys;
    WORD LangSysCount;
    GSUB_LangSysRecord LangSysRecord[1];
} GSUB_Script;

typedef struct {
    WORD LookupOrder; /* Reserved */
    WORD ReqFeatureIndex;
    WORD FeatureCount;
    WORD FeatureIndex[1];
} GSUB_LangSys;

typedef struct {
    CHAR FeatureTag[4];
    WORD Feature;
} GSUB_FeatureRecord;

typedef struct {
    WORD FeatureCount;
    GSUB_FeatureRecord FeatureRecord[1];
} GSUB_FeatureList;

typedef struct {
    WORD FeatureParams; /* Reserved */
    WORD LookupCount;
    WORD LookupListIndex[1];
} GSUB_Feature;

typedef struct {
    WORD LookupCount;
    WORD Lookup[1];
} GSUB_LookupList;

typedef struct {
    WORD LookupType;
    WORD LookupFlag;
    WORD SubTableCount;
    WORD SubTable[1];
} GSUB_LookupTable;

typedef struct {
    WORD CoverageFormat;
    WORD GlyphCount;
    WORD GlyphArray[1];
} GSUB_CoverageFormat1;

typedef struct {
    WORD Start;
    WORD End;
    WORD StartCoverageIndex;
} GSUB_RangeRecord;

typedef struct {
    WORD CoverageFormat;
    WORD RangeCount;
    GSUB_RangeRecord RangeRecord[1];
} GSUB_CoverageFormat2;

typedef struct {
    WORD SubstFormat; /* = 1 */
    WORD Coverage;
    WORD DeltaGlyphID;
} GSUB_SingleSubstFormat1;

typedef struct {
    WORD SubstFormat; /* = 2 */
    WORD Coverage;
    WORD GlyphCount;
    WORD Substitute[1];
}GSUB_SingleSubstFormat2;

typedef struct {
    WORD SubstFormat; /* = 1 */
    WORD Coverage;
    WORD LigSetCount;
    WORD LigatureSet[1];
}GSUB_LigatureSubstFormat1;

typedef struct {
    WORD LigatureCount;
    WORD Ligature[1];
}GSUB_LigatureSet;

typedef struct{
    WORD LigGlyph;
    WORD CompCount;
    WORD Component[1];
}GSUB_Ligature;

typedef struct{
    WORD SequenceIndex;
    WORD LookupListIndex;

}GSUB_SubstLookupRecord;

typedef struct{
    WORD SubstFormat; /* = 1 */
    WORD Coverage;
    WORD ChainSubRuleSetCount;
    WORD ChainSubRuleSet[1];
}GSUB_ChainContextSubstFormat1;

typedef struct {
    WORD SubstFormat; /* = 3 */
    WORD BacktrackGlyphCount;
    WORD Coverage[1];
}GSUB_ChainContextSubstFormat3_1;

typedef struct{
    WORD InputGlyphCount;
    WORD Coverage[1];
}GSUB_ChainContextSubstFormat3_2;

typedef struct{
    WORD LookaheadGlyphCount;
    WORD Coverage[1];
}GSUB_ChainContextSubstFormat3_3;

typedef struct{
    WORD SubstCount;
    GSUB_SubstLookupRecord SubstLookupRecord[1];
}GSUB_ChainContextSubstFormat3_4;

typedef struct {
    WORD SubstFormat; /* = 1 */
    WORD Coverage;
    WORD AlternateSetCount;
    WORD AlternateSet[1];
} GSUB_AlternateSubstFormat1;

typedef struct{
    WORD GlyphCount;
    WORD Alternate[1];
} GSUB_AlternateSet;

/* These are all structures needed for the GDEF table */
#define GDEF_TAG MS_MAKE_TAG('G', 'D', 'E', 'F')

enum {BaseGlyph=1, LigatureGlyph, MarkGlyph, ComponentGlyph};

typedef struct {
    DWORD Version;
    WORD GlyphClassDef;
    WORD AttachList;
    WORD LigCaretList;
    WORD MarkAttachClassDef;
} GDEF_Header;

typedef struct {
    WORD ClassFormat;
    WORD StartGlyph;
    WORD GlyphCount;
    WORD ClassValueArray[1];
} GDEF_ClassDefFormat1;

typedef struct {
    WORD Start;
    WORD End;
    WORD Class;
} GDEF_ClassRangeRecord;

typedef struct {
    WORD ClassFormat;
    WORD ClassRangeCount;
    GDEF_ClassRangeRecord ClassRangeRecord[1];
} GDEF_ClassDefFormat2;

static INT GSUB_apply_lookup(const GSUB_LookupList* lookup, INT lookup_index, WORD *glyphs, INT glyph_index, INT write_dir, INT *glyph_count);

/* the orders of joined_forms and contextual_features need to line up */
static const char* contextual_features[] =
{
    "isol",
    "fina",
    "init",
    "medi",
    /* Syriac Alaph */
    "med2",
    "fin2",
    "fin3"
};

static OPENTYPE_FEATURE_RECORD standard_features[] =
{
    { MS_MAKE_TAG('l','i','g','a'), 1},
    { MS_MAKE_TAG('c','l','i','g'), 1},
};

static OPENTYPE_FEATURE_RECORD arabic_features[] =
{
    { MS_MAKE_TAG('r','l','i','g'), 1},
    { MS_MAKE_TAG('c','a','l','t'), 1},
    { MS_MAKE_TAG('l','i','g','a'), 1},
    { MS_MAKE_TAG('d','l','i','g'), 1},
    { MS_MAKE_TAG('c','s','w','h'), 1},
    { MS_MAKE_TAG('m','s','e','t'), 1},
};

static const char* required_arabic_features[] =
{
    "fina",
    "init",
    "medi",
    "rlig",
    NULL
};

static OPENTYPE_FEATURE_RECORD hebrew_features[] =
{
    { MS_MAKE_TAG('d','l','i','g'), 1},
};

static OPENTYPE_FEATURE_RECORD syriac_features[] =
{
    { MS_MAKE_TAG('r','l','i','g'), 1},
    { MS_MAKE_TAG('c','a','l','t'), 1},
    { MS_MAKE_TAG('l','i','g','a'), 1},
    { MS_MAKE_TAG('d','l','i','g'), 1},
};

static const char* required_syriac_features[] =
{
    "fina",
    "fin2",
    "fin3",
    "init",
    "medi",
    "med2",
    "rlig",
    NULL
};

static OPENTYPE_FEATURE_RECORD sinhala_features[] =
{
    /* Base forms */
    { MS_MAKE_TAG('a','k','h','n'), 1},
    { MS_MAKE_TAG('r','p','h','f'), 1},
    { MS_MAKE_TAG('v','a','t','u'), 1},
    { MS_MAKE_TAG('p','s','t','f'), 1},
    /* Presentation forms */
    { MS_MAKE_TAG('b','l','w','s'), 1},
    { MS_MAKE_TAG('a','b','v','s'), 1},
    { MS_MAKE_TAG('p','s','t','s'), 1},
};

static OPENTYPE_FEATURE_RECORD tibetan_features[] =
{
    { MS_MAKE_TAG('a','b','v','s'), 1},
    { MS_MAKE_TAG('b','l','w','s'), 1},
};

static OPENTYPE_FEATURE_RECORD thai_features[] =
{
    { MS_MAKE_TAG('c','c','m','p'), 1},
};

static const char* required_lao_features[] =
{
    "ccmp",
    NULL
};

typedef struct ScriptShapeDataTag {
    TEXTRANGE_PROPERTIES   defaultTextRange;
    const char**           requiredFeatures;
    CHAR                   otTag[5];
    ContextualShapingProc  contextProc;
    ShapeCharGlyphPropProc charGlyphPropProc;
} ScriptShapeData;

/* in order of scripts */
static const ScriptShapeData ShapingData[] =
{
    {{ standard_features, 2}, NULL, "", NULL, NULL},
    {{ standard_features, 2}, NULL, "latn", NULL, NULL},
    {{ standard_features, 2}, NULL, "latn", NULL, NULL},
    {{ standard_features, 2}, NULL, "latn", NULL, NULL},
    {{ standard_features, 2}, NULL, "" , NULL, NULL},
    {{ standard_features, 2}, NULL, "latn", NULL, NULL},
    {{ arabic_features, 6}, required_arabic_features, "arab", ContextualShape_Arabic, ShapeCharGlyphProp_Arabic},
    {{ arabic_features, 6}, required_arabic_features, "arab", ContextualShape_Arabic, ShapeCharGlyphProp_Arabic},
    {{ hebrew_features, 1}, NULL, "hebr", NULL, NULL},
    {{ syriac_features, 4}, required_syriac_features, "syrc", ContextualShape_Syriac, ShapeCharGlyphProp_None},
    {{ arabic_features, 6}, required_arabic_features, "arab", ContextualShape_Arabic, ShapeCharGlyphProp_Arabic},
    {{ NULL, 0}, NULL, "thaa", NULL, ShapeCharGlyphProp_None},
    {{ standard_features, 2}, NULL, "grek", NULL, NULL},
    {{ standard_features, 2}, NULL, "cyrl", NULL, NULL},
    {{ standard_features, 2}, NULL, "armn", NULL, NULL},
    {{ standard_features, 2}, NULL, "geor", NULL, NULL},
    {{ sinhala_features, 7}, NULL, "sinh", NULL, NULL},
    {{ tibetan_features, 2}, NULL, "tibt", NULL, ShapeCharGlyphProp_None},
    {{ tibetan_features, 2}, NULL, "tibt", NULL, ShapeCharGlyphProp_None},
    {{ tibetan_features, 2}, NULL, "phag", ContextualShape_Phags_pa, ShapeCharGlyphProp_Thai},
    {{ thai_features, 1}, NULL, "thai", NULL, ShapeCharGlyphProp_Thai},
    {{ thai_features, 1}, NULL, "thai", NULL, ShapeCharGlyphProp_Thai},
    {{ thai_features, 1}, required_lao_features, "lao", NULL, ShapeCharGlyphProp_Thai},
    {{ thai_features, 1}, required_lao_features, "lao", NULL, ShapeCharGlyphProp_Thai},
};

static INT GSUB_is_glyph_covered(LPCVOID table , UINT glyph)
{
    const GSUB_CoverageFormat1* cf1;

    cf1 = table;

    if (GET_BE_WORD(cf1->CoverageFormat) == 1)
    {
        int count = GET_BE_WORD(cf1->GlyphCount);
        int i;
        TRACE("Coverage Format 1, %i glyphs\n",count);
        for (i = 0; i < count; i++)
            if (glyph == GET_BE_WORD(cf1->GlyphArray[i]))
                return i;
        return -1;
    }
    else if (GET_BE_WORD(cf1->CoverageFormat) == 2)
    {
        const GSUB_CoverageFormat2* cf2;
        int i;
        int count;
        cf2 = (const GSUB_CoverageFormat2*)cf1;

        count = GET_BE_WORD(cf2->RangeCount);
        TRACE("Coverage Format 2, %i ranges\n",count);
        for (i = 0; i < count; i++)
        {
            if (glyph < GET_BE_WORD(cf2->RangeRecord[i].Start))
                return -1;
            if ((glyph >= GET_BE_WORD(cf2->RangeRecord[i].Start)) &&
                (glyph <= GET_BE_WORD(cf2->RangeRecord[i].End)))
            {
                return (GET_BE_WORD(cf2->RangeRecord[i].StartCoverageIndex) +
                    glyph - GET_BE_WORD(cf2->RangeRecord[i].Start));
            }
        }
        return -1;
    }
    else
        ERR("Unknown CoverageFormat %i\n",GET_BE_WORD(cf1->CoverageFormat));

    return -1;
}

static const GSUB_Script* GSUB_get_script_table( const GSUB_Header* header, const char* tag)
{
    const GSUB_ScriptList *script;
    const GSUB_Script *deflt = NULL;
    int i;
    script = (const GSUB_ScriptList*)((const BYTE*)header + GET_BE_WORD(header->ScriptList));

    TRACE("%i scripts in this font\n",GET_BE_WORD(script->ScriptCount));
    for (i = 0; i < GET_BE_WORD(script->ScriptCount); i++)
    {
        const GSUB_Script *scr;
        int offset;

        offset = GET_BE_WORD(script->ScriptRecord[i].Script);
        scr = (const GSUB_Script*)((const BYTE*)script + offset);

        if (strncmp(script->ScriptRecord[i].ScriptTag, tag,4)==0)
            return scr;
        if (strncmp(script->ScriptRecord[i].ScriptTag, "dflt",4)==0)
            deflt = scr;
    }
    return deflt;
}

static const GSUB_LangSys* GSUB_get_lang_table( const GSUB_Script* script, const char* tag)
{
    int i;
    int offset;
    const GSUB_LangSys *Lang;

    TRACE("Deflang %x, LangCount %i\n",GET_BE_WORD(script->DefaultLangSys), GET_BE_WORD(script->LangSysCount));

    for (i = 0; i < GET_BE_WORD(script->LangSysCount) ; i++)
    {
        offset = GET_BE_WORD(script->LangSysRecord[i].LangSys);
        Lang = (const GSUB_LangSys*)((const BYTE*)script + offset);

        if ( strncmp(script->LangSysRecord[i].LangSysTag,tag,4)==0)
            return Lang;
    }
    offset = GET_BE_WORD(script->DefaultLangSys);
    if (offset)
    {
        Lang = (const GSUB_LangSys*)((const BYTE*)script + offset);
        return Lang;
    }
    return NULL;
}

static const GSUB_Feature * GSUB_get_feature(const GSUB_Header *header, const GSUB_LangSys *lang, const char* tag)
{
    int i;
    const GSUB_FeatureList *feature;
    feature = (const GSUB_FeatureList*)((const BYTE*)header + GET_BE_WORD(header->FeatureList));

    TRACE("%i features\n",GET_BE_WORD(lang->FeatureCount));
    for (i = 0; i < GET_BE_WORD(lang->FeatureCount); i++)
    {
        int index = GET_BE_WORD(lang->FeatureIndex[i]);
        if (strncmp(feature->FeatureRecord[index].FeatureTag,tag,4)==0)
        {
            const GSUB_Feature *feat;
            feat = (const GSUB_Feature*)((const BYTE*)feature + GET_BE_WORD(feature->FeatureRecord[index].Feature));
            return feat;
        }
    }
    return NULL;
}

static INT GSUB_apply_SingleSubst(const GSUB_LookupTable *look, WORD *glyphs, INT glyph_index, INT write_dir, INT *glyph_count)
{
    int j;
    TRACE("Single Substitution Subtable\n");

    for (j = 0; j < GET_BE_WORD(look->SubTableCount); j++)
    {
        int offset;
        const GSUB_SingleSubstFormat1 *ssf1;
        offset = GET_BE_WORD(look->SubTable[j]);
        ssf1 = (const GSUB_SingleSubstFormat1*)((const BYTE*)look+offset);
        if (GET_BE_WORD(ssf1->SubstFormat) == 1)
        {
            int offset = GET_BE_WORD(ssf1->Coverage);
            TRACE("  subtype 1, delta %i\n", GET_BE_WORD(ssf1->DeltaGlyphID));
            if (GSUB_is_glyph_covered((const BYTE*)ssf1+offset, glyphs[glyph_index]) != -1)
            {
                TRACE("  Glyph 0x%x ->",glyphs[glyph_index]);
                glyphs[glyph_index] = glyphs[glyph_index] + GET_BE_WORD(ssf1->DeltaGlyphID);
                TRACE(" 0x%x\n",glyphs[glyph_index]);
                return glyph_index + 1;
            }
        }
        else
        {
            const GSUB_SingleSubstFormat2 *ssf2;
            INT index;
            INT offset;

            ssf2 = (const GSUB_SingleSubstFormat2 *)ssf1;
            offset = GET_BE_WORD(ssf1->Coverage);
            TRACE("  subtype 2,  glyph count %i\n", GET_BE_WORD(ssf2->GlyphCount));
            index = GSUB_is_glyph_covered((const BYTE*)ssf2+offset, glyphs[glyph_index]);
            TRACE("  Coverage index %i\n",index);
            if (index != -1)
            {
                if (glyphs[glyph_index] == GET_BE_WORD(ssf2->Substitute[index]))
                    return GSUB_E_NOGLYPH;

                TRACE("    Glyph is 0x%x ->",glyphs[glyph_index]);
                glyphs[glyph_index] = GET_BE_WORD(ssf2->Substitute[index]);
                TRACE("0x%x\n",glyphs[glyph_index]);
                return glyph_index + 1;
            }
        }
    }
    return GSUB_E_NOGLYPH;
}

static INT GSUB_apply_AlternateSubst(const GSUB_LookupTable *look, WORD *glyphs, INT glyph_index, INT write_dir, INT *glyph_count)
{
    int j;
    TRACE("Alternate Substitution Subtable\n");

    for (j = 0; j < GET_BE_WORD(look->SubTableCount); j++)
    {
        int offset;
        const GSUB_AlternateSubstFormat1 *asf1;
        INT index;

        offset = GET_BE_WORD(look->SubTable[j]);
        asf1 = (const GSUB_AlternateSubstFormat1*)((const BYTE*)look+offset);
        offset = GET_BE_WORD(asf1->Coverage);

        index = GSUB_is_glyph_covered((const BYTE*)asf1+offset, glyphs[glyph_index]);
        if (index != -1)
        {
            const GSUB_AlternateSet *as;
            offset =  GET_BE_WORD(asf1->AlternateSet[index]);
            as = (const GSUB_AlternateSet*)((const BYTE*)asf1+offset);
            FIXME("%i alternates, picking index 0\n",GET_BE_WORD(as->GlyphCount));
            if (glyphs[glyph_index] == GET_BE_WORD(as->Alternate[0]))
                return GSUB_E_NOGLYPH;

            TRACE("  Glyph 0x%x ->",glyphs[glyph_index]);
            glyphs[glyph_index] = GET_BE_WORD(as->Alternate[0]);
            TRACE(" 0x%x\n",glyphs[glyph_index]);
            return glyph_index + 1;
        }
    }
    return GSUB_E_NOGLYPH;
}

static INT GSUB_apply_LigatureSubst(const GSUB_LookupTable *look, WORD *glyphs, INT glyph_index, INT write_dir, INT *glyph_count)
{
    int j;

    TRACE("Ligature Substitution Subtable\n");
    for (j = 0; j < GET_BE_WORD(look->SubTableCount); j++)
    {
        const GSUB_LigatureSubstFormat1 *lsf1;
        int offset,index;

        offset = GET_BE_WORD(look->SubTable[j]);
        lsf1 = (const GSUB_LigatureSubstFormat1*)((const BYTE*)look+offset);
        offset = GET_BE_WORD(lsf1->Coverage);
        index = GSUB_is_glyph_covered((const BYTE*)lsf1+offset, glyphs[glyph_index]);
        TRACE("  Coverage index %i\n",index);
        if (index != -1)
        {
            const GSUB_LigatureSet *ls;
            int k, count;

            offset = GET_BE_WORD(lsf1->LigatureSet[index]);
            ls = (const GSUB_LigatureSet*)((const BYTE*)lsf1+offset);
            count = GET_BE_WORD(ls->LigatureCount);
            TRACE("  LigatureSet has %i members\n",count);
            for (k = 0; k < count; k++)
            {
                const GSUB_Ligature *lig;
                int CompCount,l,CompIndex;

                offset = GET_BE_WORD(ls->Ligature[k]);
                lig = (const GSUB_Ligature*)((const BYTE*)ls+offset);
                CompCount = GET_BE_WORD(lig->CompCount) - 1;
                CompIndex = glyph_index+write_dir;
                for (l = 0; l < CompCount && CompIndex >= 0 && CompIndex < *glyph_count; l++)
                {
                    int CompGlyph;
                    CompGlyph = GET_BE_WORD(lig->Component[l]);
                    if (CompGlyph != glyphs[CompIndex])
                        break;
                    CompIndex += write_dir;
                }
                if (l == CompCount)
                {
                    int replaceIdx = glyph_index;
                    if (write_dir < 0)
                        replaceIdx = glyph_index - CompCount;

                    TRACE("    Glyph is 0x%x (+%i) ->",glyphs[glyph_index],CompCount);
                    glyphs[replaceIdx] = GET_BE_WORD(lig->LigGlyph);
                    TRACE("0x%x\n",glyphs[replaceIdx]);
                    if (CompCount > 0)
                    {
                        int j;
                        for (j = replaceIdx + 1; j < *glyph_count; j++)
                            glyphs[j] =glyphs[j+CompCount];
                        *glyph_count = *glyph_count - CompCount;
                    }
                    return replaceIdx + 1;
                }
            }
        }
    }
    return GSUB_E_NOGLYPH;
}

static INT GSUB_apply_ChainContextSubst(const GSUB_LookupList* lookup, const GSUB_LookupTable *look, WORD *glyphs, INT glyph_index, INT write_dir, INT *glyph_count)
{
    int j;
    BOOL done = FALSE;

    TRACE("Chaining Contextual Substitution Subtable\n");
    for (j = 0; j < GET_BE_WORD(look->SubTableCount) && !done; j++)
    {
        const GSUB_ChainContextSubstFormat1 *ccsf1;
        int offset;
        int dirLookahead = write_dir;
        int dirBacktrack = -1 * write_dir;

        offset = GET_BE_WORD(look->SubTable[j]);
        ccsf1 = (const GSUB_ChainContextSubstFormat1*)((const BYTE*)look+offset);
        if (GET_BE_WORD(ccsf1->SubstFormat) == 1)
        {
            FIXME("  TODO: subtype 1 (Simple context glyph substitution)\n");
            return -1;
        }
        else if (GET_BE_WORD(ccsf1->SubstFormat) == 2)
        {
            FIXME("  TODO: subtype 2 (Class-based Chaining Context Glyph Substitution)\n");
            return -1;
        }
        else if (GET_BE_WORD(ccsf1->SubstFormat) == 3)
        {
            int k;
            int indexGlyphs;
            const GSUB_ChainContextSubstFormat3_1 *ccsf3_1;
            const GSUB_ChainContextSubstFormat3_2 *ccsf3_2;
            const GSUB_ChainContextSubstFormat3_3 *ccsf3_3;
            const GSUB_ChainContextSubstFormat3_4 *ccsf3_4;
            int newIndex = glyph_index;

            ccsf3_1 = (const GSUB_ChainContextSubstFormat3_1 *)ccsf1;

            TRACE("  subtype 3 (Coverage-based Chaining Context Glyph Substitution)\n");

            for (k = 0; k < GET_BE_WORD(ccsf3_1->BacktrackGlyphCount); k++)
            {
                offset = GET_BE_WORD(ccsf3_1->Coverage[k]);
                if (GSUB_is_glyph_covered((const BYTE*)ccsf3_1+offset, glyphs[glyph_index + (dirBacktrack * (k+1))]) == -1)
                    break;
            }
            if (k != GET_BE_WORD(ccsf3_1->BacktrackGlyphCount))
                return -1;
            TRACE("Matched Backtrack\n");

            ccsf3_2 = (const GSUB_ChainContextSubstFormat3_2 *)(((LPBYTE)ccsf1)+sizeof(GSUB_ChainContextSubstFormat3_1) + (sizeof(WORD) * (GET_BE_WORD(ccsf3_1->BacktrackGlyphCount)-1)));

            indexGlyphs = GET_BE_WORD(ccsf3_2->InputGlyphCount);
            for (k = 0; k < indexGlyphs; k++)
            {
                offset = GET_BE_WORD(ccsf3_2->Coverage[k]);
                if (GSUB_is_glyph_covered((const BYTE*)ccsf3_1+offset, glyphs[glyph_index + (write_dir * k)]) == -1)
                    break;
            }
            if (k != indexGlyphs)
                return -1;
            TRACE("Matched IndexGlyphs\n");

            ccsf3_3 = (const GSUB_ChainContextSubstFormat3_3 *)(((LPBYTE)ccsf3_2)+sizeof(GSUB_ChainContextSubstFormat3_2) + (sizeof(WORD) * (GET_BE_WORD(ccsf3_2->InputGlyphCount)-1)));

            for (k = 0; k < GET_BE_WORD(ccsf3_3->LookaheadGlyphCount); k++)
            {
                offset = GET_BE_WORD(ccsf3_3->Coverage[k]);
                if (GSUB_is_glyph_covered((const BYTE*)ccsf3_1+offset, glyphs[glyph_index + (dirLookahead * (indexGlyphs + k))]) == -1)
                    break;
            }
            if (k != GET_BE_WORD(ccsf3_3->LookaheadGlyphCount))
                return -1;
            TRACE("Matched LookAhead\n");

            ccsf3_4 = (const GSUB_ChainContextSubstFormat3_4 *)(((LPBYTE)ccsf3_3)+sizeof(GSUB_ChainContextSubstFormat3_3) + (sizeof(WORD) * (GET_BE_WORD(ccsf3_3->LookaheadGlyphCount)-1)));

            for (k = 0; k < GET_BE_WORD(ccsf3_4->SubstCount); k++)
            {
                int lookupIndex = GET_BE_WORD(ccsf3_4->SubstLookupRecord[k].LookupListIndex);
                int SequenceIndex = GET_BE_WORD(ccsf3_4->SubstLookupRecord[k].SequenceIndex) * write_dir;

                TRACE("SUBST: %i -> %i %i\n",k, SequenceIndex, lookupIndex);
                newIndex = GSUB_apply_lookup(lookup, lookupIndex, glyphs, glyph_index + SequenceIndex, write_dir, glyph_count);
                if (newIndex == -1)
                {
                    ERR("Chain failed to generate a glyph\n");
                    return -1;
                }
            }
            return newIndex + 1;
        }
    }
    return -1;
}

static INT GSUB_apply_lookup(const GSUB_LookupList* lookup, INT lookup_index, WORD *glyphs, INT glyph_index, INT write_dir, INT *glyph_count)
{
    int offset;
    const GSUB_LookupTable *look;

    offset = GET_BE_WORD(lookup->Lookup[lookup_index]);
    look = (const GSUB_LookupTable*)((const BYTE*)lookup + offset);
    TRACE("type %i, flag %x, subtables %i\n",GET_BE_WORD(look->LookupType),GET_BE_WORD(look->LookupFlag),GET_BE_WORD(look->SubTableCount));
    switch(GET_BE_WORD(look->LookupType))
    {
        case 1:
            return GSUB_apply_SingleSubst(look, glyphs, glyph_index, write_dir, glyph_count);
        case 3:
            return GSUB_apply_AlternateSubst(look, glyphs, glyph_index, write_dir, glyph_count);
        case 4:
            return GSUB_apply_LigatureSubst(look, glyphs, glyph_index, write_dir, glyph_count);
        case 6:
            return GSUB_apply_ChainContextSubst(lookup, look, glyphs, glyph_index, write_dir, glyph_count);
        default:
            FIXME("We do not handle SubType %i\n",GET_BE_WORD(look->LookupType));
    }
    return GSUB_E_NOGLYPH;
}

static INT GSUB_apply_feature(const GSUB_Header * header, const GSUB_Feature* feature, WORD *glyphs, INT glyph_index, INT write_dir, INT *glyph_count)
{
    int i;
    int out_index = GSUB_E_NOGLYPH;
    const GSUB_LookupList *lookup;

    lookup = (const GSUB_LookupList*)((const BYTE*)header + GET_BE_WORD(header->LookupList));

    TRACE("%i lookups\n", GET_BE_WORD(feature->LookupCount));
    for (i = 0; i < GET_BE_WORD(feature->LookupCount); i++)
    {
        out_index = GSUB_apply_lookup(lookup, GET_BE_WORD(feature->LookupListIndex[i]), glyphs, glyph_index, write_dir, glyph_count);
        if (out_index != GSUB_E_NOGLYPH)
            break;
    }
    if (out_index == GSUB_E_NOGLYPH)
        TRACE("lookups found no glyphs\n");
    else
    {
        int out2;
        out2 = GSUB_apply_feature(header, feature, glyphs, glyph_index, write_dir, glyph_count);
        if (out2!=GSUB_E_NOGLYPH)
            out_index = out2;
    }
    return out_index;
}

static const char* get_opentype_script(HDC hdc, SCRIPT_ANALYSIS *psa, ScriptCache *psc)
{
    UINT charset;

    if (psc->userScript != 0)
        return (char*)&psc->userScript;

    if (ShapingData[psa->eScript].otTag[0] != 0)
        return ShapingData[psa->eScript].otTag;

    /*
     * fall back to the font charset
     */
    charset = GetTextCharsetInfo(hdc, NULL, 0x0);
    switch (charset)
    {
        case ANSI_CHARSET: return "latn";
        case BALTIC_CHARSET: return "latn"; /* ?? */
        case CHINESEBIG5_CHARSET: return "hani";
        case EASTEUROPE_CHARSET: return "latn"; /* ?? */
        case GB2312_CHARSET: return "hani";
        case GREEK_CHARSET: return "grek";
        case HANGUL_CHARSET: return "hang";
        case RUSSIAN_CHARSET: return "cyrl";
        case SHIFTJIS_CHARSET: return "kana";
        case TURKISH_CHARSET: return "latn"; /* ?? */
        case VIETNAMESE_CHARSET: return "latn";
        case JOHAB_CHARSET: return "latn"; /* ?? */
        case ARABIC_CHARSET: return "arab";
        case HEBREW_CHARSET: return "hebr";
        case THAI_CHARSET: return "thai";
        default: return "latn";
    }
}

static LPCVOID load_GSUB_feature(HDC hdc, SCRIPT_ANALYSIS *psa, ScriptCache *psc, const char* feat)
{
    const GSUB_Feature *feature;
    int i;

    for (i = 0; i <  psc->feature_count; i++)
        if (strncmp(psc->features[i].tag,feat,4)==0)
            return psc->features[i].feature;

    feature = NULL;

    if (psc->GSUB_Table)
    {
        const GSUB_Script *script;
        const GSUB_LangSys *language;

        script = GSUB_get_script_table(psc->GSUB_Table, get_opentype_script(hdc,psa,psc));
        if (script)
        {
            if (psc->userLang != 0)
                language = GSUB_get_lang_table(script,(char*)&psc->userLang);
            else
                language = GSUB_get_lang_table(script, "xxxx"); /* Need to get Lang tag */
            if (language)
                feature = GSUB_get_feature(psc->GSUB_Table, language, feat);
        }

        /* try in the default (latin) table */
        if (!feature)
        {
            script = GSUB_get_script_table(psc->GSUB_Table, "latn");
            if (script)
            {
                language = GSUB_get_lang_table(script, "xxxx"); /* Need to get Lang tag */
                if (language)
                    feature = GSUB_get_feature(psc->GSUB_Table, language, feat);
            }
        }
    }

    TRACE("Feature %s located at %p\n",debugstr_an(feat,4),feature);

    psc->feature_count++;

    if (psc->features)
        psc->features = HeapReAlloc(GetProcessHeap(), 0, psc->features, psc->feature_count * sizeof(LoadedFeature));
    else
        psc->features = HeapAlloc(GetProcessHeap(), 0, psc->feature_count * sizeof(LoadedFeature));

    lstrcpynA(psc->features[psc->feature_count - 1].tag, feat, 5);
    psc->features[psc->feature_count - 1].feature = feature;
    return feature;
}

static INT apply_GSUB_feature_to_glyph(HDC hdc, SCRIPT_ANALYSIS *psa, ScriptCache* psc, WORD *glyphs, INT index, INT write_dir, INT* glyph_count, const char* feat)
{
    const GSUB_Feature *feature;

    feature = load_GSUB_feature(hdc, psa, psc, feat);
    if (!feature)
        return GSUB_E_NOFEATURE;

    TRACE("applying feature %s\n",feat);
    return GSUB_apply_feature(psc->GSUB_Table, feature, glyphs, index, write_dir, glyph_count);
}

static VOID *load_gsub_table(HDC hdc)
{
    VOID* GSUB_Table = NULL;
    int length = GetFontData(hdc, GSUB_TAG , 0, NULL, 0);
    if (length != GDI_ERROR)
    {
        GSUB_Table = HeapAlloc(GetProcessHeap(),0,length);
        GetFontData(hdc, GSUB_TAG , 0, GSUB_Table, length);
        TRACE("Loaded GSUB table of %i bytes\n",length);
    }
    return GSUB_Table;
}

static WORD GDEF_get_glyph_class(const GDEF_Header *header, WORD glyph)
{
    int offset;
    WORD class = 0;
    const GDEF_ClassDefFormat1 *cf1;

    if (!header)
        return 0;

    offset = GET_BE_WORD(header->GlyphClassDef);
    if (!offset)
        return 0;

    cf1 = (GDEF_ClassDefFormat1*)(((BYTE*)header)+offset);
    if (GET_BE_WORD(cf1->ClassFormat) == 1)
    {
        if (glyph >= GET_BE_WORD(cf1->StartGlyph))
        {
            int index = glyph - GET_BE_WORD(cf1->StartGlyph);
            if (index < GET_BE_WORD(cf1->GlyphCount))
                class = GET_BE_WORD(cf1->ClassValueArray[index]);
        }
    }
    else if (GET_BE_WORD(cf1->ClassFormat) == 2)
    {
        const GDEF_ClassDefFormat2 *cf2 = (GDEF_ClassDefFormat2*)cf1;
        int i, top;
        top = GET_BE_WORD(cf2->ClassRangeCount);
        for (i = 0; i < top; i++)
        {
            if (glyph >= GET_BE_WORD(cf2->ClassRangeRecord[i].Start) &&
                glyph <= GET_BE_WORD(cf2->ClassRangeRecord[i].End))
            {
                class = GET_BE_WORD(cf2->ClassRangeRecord[i].Class);
                break;
            }
        }
    }
    else
        ERR("Unknown Class Format %i\n",GET_BE_WORD(cf1->ClassFormat));

    return class;
}

static VOID *load_gdef_table(HDC hdc)
{
    VOID* GDEF_Table = NULL;
    int length = GetFontData(hdc, GDEF_TAG , 0, NULL, 0);
    if (length != GDI_ERROR)
    {
        GDEF_Table = HeapAlloc(GetProcessHeap(),0,length);
        GetFontData(hdc, GDEF_TAG , 0, GDEF_Table, length);
        TRACE("Loaded GDEF table of %i bytes\n",length);
    }
    return GDEF_Table;
}

static void GDEF_UpdateGlyphProps(HDC hdc, const WORD *pwGlyphs, const WORD cGlyphs, WORD* pwLogClust, SCRIPT_GLYPHPROP *pGlyphProp)
{
    VOID* header = load_gdef_table(hdc);
    int i;

    for (i = 0; i < cGlyphs; i++)
    {
        WORD class;

        class = GDEF_get_glyph_class(header, pwGlyphs[i]);

        switch (class)
        {
            case 0:
            case BaseGlyph:
                pGlyphProp[i].sva.fClusterStart = 1;
                pGlyphProp[i].sva.fDiacritic = 0;
                pGlyphProp[i].sva.fZeroWidth = 0;
                break;
            case LigatureGlyph:
                pGlyphProp[i].sva.fClusterStart = 1;
                pGlyphProp[i].sva.fDiacritic = 0;
                pGlyphProp[i].sva.fZeroWidth = 0;
                break;
            case MarkGlyph:
                pGlyphProp[i].sva.fClusterStart = 0;
                pGlyphProp[i].sva.fDiacritic = 1;
                pGlyphProp[i].sva.fZeroWidth = 1;
                break;
            case ComponentGlyph:
                pGlyphProp[i].sva.fClusterStart = 0;
                pGlyphProp[i].sva.fDiacritic = 0;
                pGlyphProp[i].sva.fZeroWidth = 0;
                break;
            default:
                ERR("Unknown glyph class %i\n",class);
                pGlyphProp[i].sva.fClusterStart = 1;
                pGlyphProp[i].sva.fDiacritic = 0;
                pGlyphProp[i].sva.fZeroWidth = 0;
        }
    }
}

static void UpdateClustersFromGlyphProp(const int cGlyphs, const int cChars, WORD* pwLogClust, SCRIPT_GLYPHPROP *pGlyphProp)
{
    int i;

    for (i = 0; i < cGlyphs; i++)
    {
        if (!pGlyphProp[i].sva.fClusterStart)
        {
            int j;
            for (j = 0; j < cChars; j++)
            {
                if (pwLogClust[j] == i)
                {
                    int k = j;
                    while (!pGlyphProp[pwLogClust[k]].sva.fClusterStart && k >= 0 && k <cChars)
                        k-=1;
                    if (pGlyphProp[pwLogClust[k]].sva.fClusterStart)
                        pwLogClust[j] = pwLogClust[k];
                }
            }
        }
    }
}

static void UpdateClusters(int nextIndex, int changeCount, int write_dir, int chars, WORD* pwLogClust )
{
    if (changeCount == 0)
        return;
    else
    {
        int i;
        int target_glyph = nextIndex - 1;
        int target_index = -1;
        int replacing_glyph = -1;
        int changed = 0;

        if (write_dir > 0)
            for (i = 0; i < chars; i++)
            {
                if (pwLogClust[i] == target_glyph)
                {
                    target_index = i;
                    break;
                }
            }
        else
            for (i = chars - 1; i >= 0; i--)
            {
                if (pwLogClust[i] == target_glyph)
                {
                    target_index = i;
                    break;
                }
            }
        if (target_index == -1)
        {
            ERR("Unable to find target glyph\n");
            return;
        }

        if (changeCount < 0)
        {
            /* merge glyphs */
            for(i = target_index; i < chars && i >= 0; i+=write_dir)
            {
                if (pwLogClust[i] == target_glyph)
                    continue;
                if(pwLogClust[i] == replacing_glyph)
                    pwLogClust[i] = target_glyph;
                else
                {
                    changed--;
                    if (changed >= changeCount)
                    {
                        replacing_glyph = pwLogClust[i];
                        pwLogClust[i] = target_glyph;
                    }
                    else
                        break;
                }
            }
        }

        /* renumber trailing indexes*/
        for(i = target_index; i < chars && i >= 0; i+=write_dir)
        {
            if (pwLogClust[i] != target_glyph)
                pwLogClust[i] += changeCount;
        }
    }
}

static int apply_GSUB_feature(HDC hdc, SCRIPT_ANALYSIS *psa, ScriptCache* psc, WORD *pwOutGlyphs, int write_dir, INT* pcGlyphs, INT cChars, const char* feat, WORD *pwLogClust )
{
    int i;

    if (psc->GSUB_Table)
    {
        const GSUB_Feature *feature;

        feature = load_GSUB_feature(hdc, psa, psc, feat);
        if (!feature)
            return GSUB_E_NOFEATURE;

        i = 0;
        TRACE("applying feature %s\n",debugstr_an(feat,4));
        while(i < *pcGlyphs)
        {
                INT nextIndex;
                INT prevCount = *pcGlyphs;
                nextIndex = GSUB_apply_feature(psc->GSUB_Table, feature, pwOutGlyphs, i, write_dir, pcGlyphs);
                if (nextIndex > GSUB_E_NOGLYPH)
                {
                    UpdateClusters(nextIndex, *pcGlyphs - prevCount, write_dir, cChars, pwLogClust);
                    i = nextIndex;
                }
                else
                    i++;
        }
        return *pcGlyphs;
    }
    return GSUB_E_NOFEATURE;
}

static WCHAR neighbour_char(int i, int delta, const WCHAR* chars, INT cchLen)
{
    if (i + delta < 0)
        return 0;
    if ( i+ delta >= cchLen)
        return 0;

    i += delta;

    return chars[i];
}

static CHAR neighbour_joining_type(int i, int delta, const CHAR* context_type, INT cchLen, SCRIPT_ANALYSIS *psa)
{
    if (i + delta < 0)
    {
        if (psa->fLinkBefore)
            return jtR;
        else
            return jtU;
    }
    if ( i+ delta >= cchLen)
    {
        if (psa->fLinkAfter)
            return jtL;
        else
            return jtU;
    }

    i += delta;

    if (context_type[i] == jtT)
        return neighbour_joining_type(i,delta,context_type,cchLen,psa);
    else
        return context_type[i];
}

static inline BOOL right_join_causing(CHAR joining_type)
{
    return (joining_type == jtL || joining_type == jtD || joining_type == jtC);
}

static inline BOOL left_join_causing(CHAR joining_type)
{
    return (joining_type == jtR || joining_type == jtD || joining_type == jtC);
}

static inline BOOL word_break_causing(WCHAR chr)
{
    /* we are working within a string of characters already guareented to
       be within one script, Syriac, so we do not worry about any characers
       other than the space character outside of that range */
    return (chr == 0 || chr == 0x20 );
}

/*
 * ContextualShape_Arabic
 */
static void ContextualShape_Arabic(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WCHAR* pwcChars, INT cChars, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, WORD *pwLogClust)
{
    CHAR *context_type;
    INT *context_shape;
    INT dirR, dirL;
    int i;

    if (*pcGlyphs != cChars)
    {
        ERR("Number of Glyphs and Chars need to match at the beginning\n");
        return;
    }

    if (!psa->fLogicalOrder && psa->fRTL)
    {
        dirR = 1;
        dirL = -1;
    }
    else
    {
        dirR = -1;
        dirL = 1;
    }

    if (!psc->GSUB_Table)
        psc->GSUB_Table = load_gsub_table(hdc);

    context_type = HeapAlloc(GetProcessHeap(),0,cChars);
    context_shape = HeapAlloc(GetProcessHeap(),0,sizeof(INT) * cChars);

    for (i = 0; i < cChars; i++)
        context_type[i] = wine_shaping_table[wine_shaping_table[pwcChars[i] >> 8] + (pwcChars[i] & 0xff)];

    for (i = 0; i < cChars; i++)
    {
        if (context_type[i] == jtR && right_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)))
            context_shape[i] = Xr;
        else if (context_type[i] == jtL && left_join_causing(neighbour_joining_type(i,dirL,context_type,cChars,psa)))
            context_shape[i] = Xl;
        else if (context_type[i] == jtD && left_join_causing(neighbour_joining_type(i,dirL,context_type,cChars,psa)) && right_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)))
            context_shape[i] = Xm;
        else if (context_type[i] == jtD && right_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)))
            context_shape[i] = Xr;
        else if (context_type[i] == jtD && left_join_causing(neighbour_joining_type(i,dirL,context_type,cChars,psa)))
            context_shape[i] = Xl;
        else
            context_shape[i] = Xn;
    }

    /* Contextual Shaping */
    i = 0;
    while(i < *pcGlyphs)
    {
        BOOL shaped = FALSE;

        if (psc->GSUB_Table)
        {
            INT nextIndex;
            INT prevCount = *pcGlyphs;
            nextIndex = apply_GSUB_feature_to_glyph(hdc, psa, psc, pwOutGlyphs, i, dirL, pcGlyphs, contextual_features[context_shape[i]]);
            if (nextIndex > GSUB_E_NOGLYPH)
            {
                i = nextIndex;
                UpdateClusters(nextIndex, *pcGlyphs - prevCount, dirL, cChars, pwLogClust);
            }
            shaped = (nextIndex > GSUB_E_NOGLYPH);
        }

        if (!shaped)
        {
            WORD newGlyph = pwOutGlyphs[i];
            if (pwcChars[i] >= FIRST_ARABIC_CHAR && pwcChars[i] <= LAST_ARABIC_CHAR)
            {
                /* fall back to presentation form B */
                WCHAR context_char = wine_shaping_forms[pwcChars[i] - FIRST_ARABIC_CHAR][context_shape[i]];
                if (context_char != pwcChars[i] && GetGlyphIndicesW(hdc, &context_char, 1, &newGlyph, 0) != GDI_ERROR && newGlyph != 0x0000)
                    pwOutGlyphs[i] = newGlyph;
            }
            i++;
        }
    }

    HeapFree(GetProcessHeap(),0,context_shape);
    HeapFree(GetProcessHeap(),0,context_type);
}

/*
 * ContextualShape_Syriac
 */

#define ALAPH 0x710
#define DALATH 0x715
#define RISH 0x72A

static void ContextualShape_Syriac(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WCHAR* pwcChars, INT cChars, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, WORD *pwLogClust)
{
    CHAR *context_type;
    INT *context_shape;
    INT dirR, dirL;
    int i;

    if (*pcGlyphs != cChars)
    {
        ERR("Number of Glyphs and Chars need to match at the beginning\n");
        return;
    }

    if (!psa->fLogicalOrder && psa->fRTL)
    {
        dirR = 1;
        dirL = -1;
    }
    else
    {
        dirR = -1;
        dirL = 1;
    }

    if (!psc->GSUB_Table)
        psc->GSUB_Table = load_gsub_table(hdc);

    if (!psc->GSUB_Table)
        return;

    context_type = HeapAlloc(GetProcessHeap(),0,cChars);
    context_shape = HeapAlloc(GetProcessHeap(),0,sizeof(INT) * cChars);

    for (i = 0; i < cChars; i++)
        context_type[i] = wine_shaping_table[wine_shaping_table[pwcChars[i] >> 8] + (pwcChars[i] & 0xff)];

    for (i = 0; i < cChars; i++)
    {
        if (pwcChars[i] == ALAPH)
        {
            WCHAR rchar = neighbour_char(i,dirR,pwcChars,cChars);

            if (left_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)) && word_break_causing(neighbour_char(i,dirL,pwcChars,cChars)))
            context_shape[i] = Afj;
            else if ( rchar != DALATH && rchar != RISH &&
!left_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)) &&
word_break_causing(neighbour_char(i,dirL,pwcChars,cChars)))
            context_shape[i] = Afn;
            else if ( (rchar == DALATH || rchar == RISH) && word_break_causing(neighbour_char(i,dirL,pwcChars,cChars)))
            context_shape[i] = Afx;
            else
            context_shape[i] = Xn;
        }
        else if (context_type[i] == jtR &&
right_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)))
            context_shape[i] = Xr;
        else if (context_type[i] == jtL && left_join_causing(neighbour_joining_type(i,dirL,context_type,cChars,psa)))
            context_shape[i] = Xl;
        else if (context_type[i] == jtD && left_join_causing(neighbour_joining_type(i,dirL,context_type,cChars,psa)) && right_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)))
            context_shape[i] = Xm;
        else if (context_type[i] == jtD && right_join_causing(neighbour_joining_type(i,dirR,context_type,cChars,psa)))
            context_shape[i] = Xr;
        else if (context_type[i] == jtD && left_join_causing(neighbour_joining_type(i,dirL,context_type,cChars,psa)))
            context_shape[i] = Xl;
        else
            context_shape[i] = Xn;
    }

    /* Contextual Shaping */
    i = 0;
    while(i < *pcGlyphs)
    {
        INT nextIndex;
        INT prevCount = *pcGlyphs;
        nextIndex = apply_GSUB_feature_to_glyph(hdc, psa, psc, pwOutGlyphs, i, dirL, pcGlyphs, contextual_features[context_shape[i]]);
        if (nextIndex > GSUB_E_NOGLYPH)
        {
            UpdateClusters(nextIndex, *pcGlyphs - prevCount, dirL, cChars, pwLogClust);
            i = nextIndex;
        }
        else
            i++;
    }

    HeapFree(GetProcessHeap(),0,context_shape);
    HeapFree(GetProcessHeap(),0,context_type);
}

/*
 * ContextualShape_Phags_pa
 */

#define phags_pa_CANDRABINDU  0xA873
#define phags_pa_START 0xA840
#define phags_pa_END  0xA87F

static void ContextualShape_Phags_pa(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WCHAR* pwcChars, INT cChars, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, WORD *pwLogClust)
{
    INT *context_shape;
    INT dirR, dirL;
    int i;

    if (*pcGlyphs != cChars)
    {
        ERR("Number of Glyphs and Chars need to match at the beginning\n");
        return;
    }

    if (!psa->fLogicalOrder && psa->fRTL)
    {
        dirR = 1;
        dirL = -1;
    }
    else
    {
        dirR = -1;
        dirL = 1;
    }

    if (!psc->GSUB_Table)
        psc->GSUB_Table = load_gsub_table(hdc);

    if (!psc->GSUB_Table)
        return;

    context_shape = HeapAlloc(GetProcessHeap(),0,sizeof(INT) * cChars);

    for (i = 0; i < cChars; i++)
    {
        if (pwcChars[i] >= phags_pa_START && pwcChars[i] <=  phags_pa_END)
        {
            WCHAR rchar = neighbour_char(i,dirR,pwcChars,cChars);
            WCHAR lchar = neighbour_char(i,dirL,pwcChars,cChars);
            BOOL jrchar = (rchar != phags_pa_CANDRABINDU && rchar >= phags_pa_START && rchar <=  phags_pa_END);
            BOOL jlchar = (lchar != phags_pa_CANDRABINDU && lchar >= phags_pa_START && lchar <=  phags_pa_END);

            if (jrchar && jlchar)
                context_shape[i] = Xm;
            else if (jrchar)
                context_shape[i] = Xr;
            else if (jlchar)
                context_shape[i] = Xl;
            else
                context_shape[i] = Xn;
        }
        else
            context_shape[i] = -1;
    }

    /* Contextual Shaping */
    i = 0;
    while(i < *pcGlyphs)
    {
        if (context_shape[i] >= 0)
        {
            INT nextIndex;
            INT prevCount = *pcGlyphs;
            nextIndex = apply_GSUB_feature_to_glyph(hdc, psa, psc, pwOutGlyphs, i, dirL, pcGlyphs, contextual_features[context_shape[i]]);
            if (nextIndex > GSUB_E_NOGLYPH)
            {
                UpdateClusters(nextIndex, *pcGlyphs - prevCount, dirL, cChars, pwLogClust);
                i = nextIndex;
            }
            else
                i++;
        }
        else
            i++;
    }

    HeapFree(GetProcessHeap(),0,context_shape);
}

static void ShapeCharGlyphProp_Default( HDC hdc, ScriptCache* psc, SCRIPT_ANALYSIS* psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD* pwLogClust, SCRIPT_CHARPROP* pCharProp, SCRIPT_GLYPHPROP* pGlyphProp)
{
    int i,k;

    for (i = 0; i < cGlyphs; i++)
    {
        int char_index[20];
        int char_count = 0;

        for (k = 0; k < cChars; k++)
        {
            if (pwLogClust[k] == i)
            {
                char_index[char_count] = k;
                char_count++;
            }
        }

        if (char_count == 0)
        {
            FIXME("No chars in this glyph?  Must be an error\n");
            continue;
        }

        if (char_count ==1 && pwcChars[char_index[0]] == 0x0020)  /* space */
        {
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_BLANK;
            pCharProp[char_index[0]].fCanGlyphAlone = 1;
        }
        else
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_CHARACTER;
    }

    GDEF_UpdateGlyphProps(hdc, pwGlyphs, cGlyphs, pwLogClust, pGlyphProp);
    UpdateClustersFromGlyphProp(cGlyphs, cChars, pwLogClust, pGlyphProp);
}

static void ShapeCharGlyphProp_Arabic( HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD *pwLogClust, SCRIPT_CHARPROP *pCharProp, SCRIPT_GLYPHPROP *pGlyphProp )
{
    int i,k;
    int initGlyph, finaGlyph;
    INT dirR, dirL;
    BYTE *spaces;

    spaces = HeapAlloc(GetProcessHeap(),0,cGlyphs);
    memset(spaces,0,cGlyphs);

    if (!psa->fLogicalOrder && psa->fRTL)
    {
        initGlyph = cGlyphs-1;
        finaGlyph = 0;
        dirR = 1;
        dirL = -1;
    }
    else
    {
        initGlyph = 0;
        finaGlyph = cGlyphs-1;
        dirR = -1;
        dirL = 1;
    }

    for (i = 0; i < cGlyphs; i++)
    {
        for (k = 0; k < cChars; k++)
            if (pwLogClust[k] == i)
            {
                if (pwcChars[k] == 0x0020)
                    spaces[i] = 1;
            }
    }

    for (i = 0; i < cGlyphs; i++)
    {
        int char_index[20];
        int char_count = 0;
        BOOL isInit, isFinal;

        for (k = 0; k < cChars; k++)
        {
            if (pwLogClust[k] == i)
            {
                char_index[char_count] = k;
                char_count++;
            }
        }

        isInit = (i == initGlyph || (i+dirR > 0 && i+dirR < cGlyphs && spaces[i+dirR]));
        isFinal = (i == finaGlyph || (i+dirL > 0 && i+dirL < cGlyphs && spaces[i+dirL]));

        if (char_count == 0)
        {
            FIXME("No chars in this glyph?  Must be an error\n");
            continue;
        }

        if (char_count == 1)
        {
            if (pwcChars[char_index[0]] == 0x0020)  /* space */
            {
                pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_BLANK;
                pCharProp[char_index[0]].fCanGlyphAlone = 1;
            }
            else if (pwcChars[char_index[0]] == 0x0640)  /* kashida */
                pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_KASHIDA;
            else if (pwcChars[char_index[0]] == 0x0633)  /* SEEN */
            {
                if (!isInit && !isFinal)
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_SEEN_M;
                else if (isInit)
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_SEEN;
                else
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_NONE;
            }
            else if (!isInit)
            {
                if (pwcChars[char_index[0]] == 0x0628 ) /* BA */
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_BA;
                else if (pwcChars[char_index[0]] == 0x0631 ) /* RA */
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_RA;
                else if (pwcChars[char_index[0]] == 0x0647 ) /* HA */
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_HA;
                else if ((pwcChars[char_index[0]] == 0x0627 || pwcChars[char_index[0]] == 0x0625 || pwcChars[char_index[0]] == 0x0623 || pwcChars[char_index[0]] == 0x0622) ) /* alef-like */
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_ALEF;
                else
                    pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_NONE;
            }
            else if (!isInit && !isFinal)
                pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_NORMAL;
            else
                pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_NONE;
        }
        else if (char_count == 2)
        {
            if ((pwcChars[char_index[0]] == 0x0628 && pwcChars[char_index[1]]== 0x0631) ||  (pwcChars[char_index[0]] == 0x0631 && pwcChars[char_index[1]]== 0x0628)) /* BA+RA */
                pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_BARA;
            else if (!isInit)
                pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_NORMAL;
            else
                pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_NONE;
        }
        else if (!isInit && !isFinal)
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_ARABIC_NORMAL;
        else
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_NONE;
    }

    GDEF_UpdateGlyphProps(hdc, pwGlyphs, cGlyphs, pwLogClust, pGlyphProp);
    UpdateClustersFromGlyphProp(cGlyphs, cChars, pwLogClust, pGlyphProp);
    HeapFree(GetProcessHeap(),0,spaces);
}

static void ShapeCharGlyphProp_Thai( HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD *pwLogClust, SCRIPT_CHARPROP *pCharProp, SCRIPT_GLYPHPROP *pGlyphProp )
{
    int i,k;
    int finaGlyph;
    INT dirL;
    BYTE *spaces;

    spaces = HeapAlloc(GetProcessHeap(),0,cGlyphs);
    memset(spaces,0,cGlyphs);

    if (!psa->fLogicalOrder && psa->fRTL)
    {
        finaGlyph = 0;
        dirL = -1;
    }
    else
    {
        finaGlyph = cGlyphs-1;
        dirL = 1;
    }

    for (i = 0; i < cGlyphs; i++)
    {
        for (k = 0; k < cChars; k++)
            if (pwLogClust[k] == i)
            {
                if (pwcChars[k] == 0x0020)
                    spaces[i] = 1;
            }
    }

    for (i = 0; i < cGlyphs; i++)
    {
        int char_index[20];
        int char_count = 0;

        for (k = 0; k < cChars; k++)
        {
            if (pwLogClust[k] == i)
            {
                char_index[char_count] = k;
                char_count++;
            }
        }

        if (char_count == 0)
        {
            FIXME("No chars in this glyph?  Must be an error\n");
            continue;
        }

        if (char_count ==1 && pwcChars[char_index[0]] == 0x0020)  /* space */
        {
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_CHARACTER;
            pCharProp[char_index[0]].fCanGlyphAlone = 1;
        }
        else if (i == finaGlyph)
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_NONE;
        else
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_CHARACTER;
    }

    HeapFree(GetProcessHeap(),0,spaces);
    GDEF_UpdateGlyphProps(hdc, pwGlyphs, cGlyphs, pwLogClust, pGlyphProp);
    UpdateClustersFromGlyphProp(cGlyphs, cChars, pwLogClust, pGlyphProp);

    /* Do not allow justification between marks and their base */
    for (i = 0; i < cGlyphs; i++)
    {
        if (!pGlyphProp[i].sva.fClusterStart)
            pGlyphProp[i-dirL].sva.uJustification = SCRIPT_JUSTIFY_NONE;
    }
}

static void ShapeCharGlyphProp_None( HDC hdc, ScriptCache* psc, SCRIPT_ANALYSIS* psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD* pwLogClust, SCRIPT_CHARPROP* pCharProp, SCRIPT_GLYPHPROP* pGlyphProp)
{
    int i,k;

    for (i = 0; i < cGlyphs; i++)
    {
        int char_index[20];
        int char_count = 0;

        for (k = 0; k < cChars; k++)
        {
            if (pwLogClust[k] == i)
            {
                char_index[char_count] = k;
                char_count++;
            }
        }

        if (char_count == 0)
        {
            FIXME("No chars in this glyph?  Must be an error\n");
            continue;
        }

        if (char_count ==1 && pwcChars[char_index[0]] == 0x0020)  /* space */
        {
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_CHARACTER;
            pCharProp[char_index[0]].fCanGlyphAlone = 1;
        }
        else
            pGlyphProp[i].sva.uJustification = SCRIPT_JUSTIFY_NONE;
    }
    GDEF_UpdateGlyphProps(hdc, pwGlyphs, cGlyphs, pwLogClust, pGlyphProp);
    UpdateClustersFromGlyphProp(cGlyphs, cChars, pwLogClust, pGlyphProp);
}

void SHAPE_CharGlyphProp(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, const WCHAR* pwcChars, const INT cChars, const WORD* pwGlyphs, const INT cGlyphs, WORD *pwLogClust, SCRIPT_CHARPROP *pCharProp, SCRIPT_GLYPHPROP *pGlyphProp)
{
    if (ShapingData[psa->eScript].charGlyphPropProc)
        ShapingData[psa->eScript].charGlyphPropProc(hdc, psc, psa, pwcChars, cChars, pwGlyphs, cGlyphs, pwLogClust, pCharProp, pGlyphProp);
    else
        ShapeCharGlyphProp_Default(hdc, psc, psa, pwcChars, cChars, pwGlyphs, cGlyphs, pwLogClust, pCharProp, pGlyphProp);
}

void SHAPE_ContextualShaping(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WCHAR* pwcChars, INT cChars, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, WORD *pwLogClust)
{
    if (ShapingData[psa->eScript].contextProc)
        ShapingData[psa->eScript].contextProc(hdc, psc, psa, pwcChars, cChars, pwOutGlyphs, pcGlyphs, cMaxGlyphs, pwLogClust);
}

static void SHAPE_ApplyOpenTypeFeatures(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, INT cChars, const TEXTRANGE_PROPERTIES *rpRangeProperties, WORD *pwLogClust)
{
    int i;
    INT dirL;

    if (!rpRangeProperties)
        return;

    if (!psc->GSUB_Table)
        psc->GSUB_Table = load_gsub_table(hdc);

    if (!psc->GSUB_Table)
        return;

    if (!psa->fLogicalOrder && psa->fRTL)
        dirL = -1;
    else
        dirL = 1;

    for (i = 0; i < rpRangeProperties->cotfRecords; i++)
    {
        if (rpRangeProperties->potfRecords[i].lParameter > 0)
        apply_GSUB_feature(hdc, psa, psc, pwOutGlyphs, dirL, pcGlyphs, cChars, (const char*)&rpRangeProperties->potfRecords[i].tagFeature, pwLogClust);
    }
}

void SHAPE_ApplyDefaultOpentypeFeatures(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa, WORD* pwOutGlyphs, INT* pcGlyphs, INT cMaxGlyphs, INT cChars, WORD *pwLogClust)
{
const TEXTRANGE_PROPERTIES *rpRangeProperties;
rpRangeProperties = &ShapingData[psa->eScript].defaultTextRange;

    SHAPE_ApplyOpenTypeFeatures(hdc, psc, psa, pwOutGlyphs, pcGlyphs, cMaxGlyphs, cChars, rpRangeProperties, pwLogClust);
}

HRESULT SHAPE_CheckFontForRequiredFeatures(HDC hdc, ScriptCache *psc, SCRIPT_ANALYSIS *psa)
{
    const GSUB_Feature *feature;
    int i;

    if (!ShapingData[psa->eScript].requiredFeatures)
        return S_OK;

    if (!psc->GSUB_Table)
        psc->GSUB_Table = load_gsub_table(hdc);

    /* we need to have at least one of the required features */
    i = 0;
    while (ShapingData[psa->eScript].requiredFeatures[i])
    {
        feature = load_GSUB_feature(hdc, psa, psc, ShapingData[psa->eScript].requiredFeatures[i]);
        if (feature)
            return S_OK;
        i++;
    }

    return USP_E_SCRIPT_NOT_IN_FONT;
}
