#!/bin/sh
# Generate a neatroff output device

# ghostscript fonts directory; should be in GS_FONTPATH
FP="/path/to/gs/fonts"
# output device directory
TP="/path/to/font/devutf"
# device resolution
RES="720"
# pattern of ligatures to ignore
LIGIGN="\(ct\|st\|sp\|Rp\)"
# minimum amount of kerning to include
MINKERN="5"

test -n "$1" && FP="$1"
test -n "$2" && TP="$2"

# creating DESC
mkdir -p $TP
echo "fonts 10 R I B BI CW H HI HB S1 S" >$TP/DESC
echo "res $RES" >>$TP/DESC
echo "hor 1" >>$TP/DESC
echo "ver 1" >>$TP/DESC
echo "unitwidth 10" >>$TP/DESC

# afmconv troff_name font_path extra_mktrfn_options
afmconv()
{
	echo $1
	cat $2 | ./mkfn -a -b -r$RES -t "$1" $3 $4 $5 $6 $7 | \
		sed "/^ligatures /s/ $LIGIGN//g" >$TP/$1
}

# ttfconv troff_name font_path extra_mktrfn_options
ttfconv()
{
	echo $1
	cat $2 | ./mkfn -b -o -r$RES -t $1 -k$MINKERN $3 $4 $5 $6 $7 | \
		sed "/^ligatures /s/ $LIGIGN//g" >$TP/$1
}

# otfconv troff_name font_path extra_mktrfn_options
otfconv()
{
	TTF="/tmp/.neatmkfn.ttf"
	# convert the OTF file to TTF using fontforge
	echo -e "Open(\"$2\")\nGenerate(\"$TTF\")" | fontforge >/dev/null 2>&1
	ttfconv $1 $TTF $3 $4 $5 $6 $7
	rm $TTF
}

# The standard fonts
afmconv R	$FP/n021003l.afm	-pTimes-Roman
afmconv I	$FP/n021023l.afm	-pTimes-Italic
afmconv B	$FP/n021004l.afm	-pTimes-Bold
afmconv BI	$FP/n021024l.afm	-pTimes-BoldItalic
afmconv S	$FP/s050000l.afm	-pSymbol -s
afmconv S1	$FP/n021003l.afm	-pTimes-Roman -s
afmconv AR	$FP/a010013l.afm	-pAvantGarde-Book
afmconv AI	$FP/a010033l.afm	-pAvantGarde-BookOblique
afmconv AB	$FP/a010015l.afm	-pAvantGarde-Demi
afmconv AX	$FP/a010035l.afm	-pAvantGarde-DemiOblique
afmconv H	$FP/n019003l.afm	-pHelvetica
afmconv HI	$FP/n019023l.afm	-pHelvetica-Oblique
afmconv HB	$FP/n019004l.afm	-pHelvetica-Bold
afmconv HX	$FP/n019024l.afm	-pHelvetica-BoldOblique
afmconv Hr	$FP/n019043l.afm	-pHelvetica-Narrow
afmconv Hi	$FP/n019063l.afm	-pHelvetica-Narrow-Oblique
afmconv Hb	$FP/n019044l.afm	-pHelvetica-Narrow-Bold
afmconv Hx	$FP/n019064l.afm	-pHelvetica-Narrow-BoldOblique
afmconv KR	$FP/b018012l.afm	-pBookman-Light
afmconv KI	$FP/b018032l.afm	-pBookman-LightItalic
afmconv KB	$FP/b018015l.afm	-pBookman-Demi
afmconv KX	$FP/b018035l.afm	-pBookman-DemiItalic
afmconv NR	$FP/c059013l.afm	-pNewCenturySchlbk-Roman
afmconv NI	$FP/c059033l.afm	-pNewCenturySchlbk-Italic
afmconv NB	$FP/c059016l.afm	-pNewCenturySchlbk-Bold
afmconv NX	$FP/c059036l.afm	-pNewCenturySchlbk-BoldItalic
afmconv PA	$FP/p052003l.afm	-pPalatino-Roman
afmconv PR	$FP/p052003l.afm	-pPalatino-Roman
afmconv PI	$FP/p052023l.afm	-pPalatino-Italic
afmconv PB	$FP/p052004l.afm	-pPalatino-Bold
afmconv PX	$FP/p052024l.afm	-pPalatino-BoldItalic
afmconv C	$FP/n022003l.afm	-pCourier
afmconv CO	$FP/n022003l.afm	-pCourier
afmconv CW	$FP/n022003l.afm	-pCourier
afmconv CI	$FP/n022023l.afm	-pCourier-Oblique
afmconv CB	$FP/n022004l.afm	-pCourier-Bold
afmconv CX	$FP/n022024l.afm	-pCourier-BoldOblique
afmconv ZI	$FP/z003034l.afm	-pZapfChancery-MediumItalic
afmconv ZD	$FP/d050000l.afm	-pZapfDingbats

# The first argument of afmconv, ttfconv, and otfconv is the troff
# name of the font and their second argument is its path. Any other
# argument is passed to mkfn directly.  The postscript names of the
# fonts are inferred from the fonts themselves  To change that, you
# can specify their names via the -p argument of *conv functions.

find $FP/ -name '*.afm' | while read FN
do
	afmconv `basename $FN .afm` $FN
done

find $FP/ -name '*.ttf' | while read FN
do
	ttfconv `basename $FN .ttf` $FN
done

find $FP/ -name '*.otf' | while read FN
do
	otfconv `basename $FN .otf` $FN
done
