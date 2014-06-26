#!/bin/sh
# Generate a neatroff output device

# ghostscript font directory; it may contain otf and ttf files also
FP="/mnt/file/gs/fonts"
# output device directory
TP="/root/queue/devutf"
# device resolution
RES="720"
# pattern of ligatures to ignore
LIGIGN="\(ct\|st\|sp\|Rp\)"
# use lcdf-typetools instead of fontforge for otf files
USELCDF="0"

# creating DESC
mkdir -p $TP
echo "fonts 10 R I B BI CW H HI HB S1 S" >$TP/DESC
echo "res $RES" >>$TP/DESC
echo "hor 1" >>$TP/DESC
echo "ver 1" >>$TP/DESC
echo "unitwidth 10" >>$TP/DESC

# afmconv troff_name postscript_name font_filename
function afmconv
{
	echo $1
	cat $3 | ./mktrfn $4 -a -b -r$RES -t $1 -p $2 | \
		sed "/^ligatures /s/ $LIGIGN//g" >$TP/$1
}

# converting otf and ttf fonts; needs fontforge or lcdf-typetools
function otfconv
{
	FN="$1"
	AFM="/tmp/.mktrfn.afm"
	if [ "$USELCDF" == "1" ]; then
		cat $2 | cfftot1 2>/dev/null | t1rawafm >$AFM 2>/dev/null
	else
		echo -e "Open(\"$2\")\nGenerate(\"$AFM\")" | fontforge >/dev/null 2>&1
	fi
	cat $AFM | ./mktrfn -b -a -r$RES -t $FN -p $FN -k50 | \
		sed "/^ligatures /s/ $LIGIGN//g" >$TP/$FN
	rm $AFM
}

# The standard fonts
afmconv R	Times-Roman		$FP/n021003l.afm
afmconv I	Times-Italic		$FP/n021023l.afm
afmconv B	Times-Bold		$FP/n021004l.afm
afmconv BI	Times-BoldItalic	$FP/n021024l.afm
afmconv S	Symbol			$FP/s050000l.afm -s
afmconv S1	Times-Roman		$FP/n021003l.afm -s
afmconv AR	AvantGarde-Book		$FP/a010013l.afm
afmconv AI	AvantGarde-BookOblique	$FP/a010033l.afm
afmconv AB	AvantGarde-Demi		$FP/a010015l.afm
afmconv AX	AvantGarde-DemiOblique	$FP/a010035l.afm
afmconv H	Helvetica		$FP/n019003l.afm
afmconv HI	Helvetica-Oblique	$FP/n019023l.afm
afmconv HB	Helvetica-Bold		$FP/n019044l.afm
afmconv HX	Helvetica-BoldOblique	$FP/n019064l.afm
afmconv Hr	Helvetica-Narrow	$FP/n019043l.afm
afmconv Hi	Helvetica-Narrow-Oblique	$FP/n019063l.afm
afmconv Hb	Helvetica-Narrow-Bold	$FP/n019044l.afm
afmconv Hx	Helvetica-Narrow-BoldOblique	$FP/n019064l.afm
afmconv KR	Bookman-Light		$FP/b018012l.afm
afmconv KI	Bookman-LightItalic	$FP/b018032l.afm
afmconv KB	Bookman-Demi		$FP/b018015l.afm
afmconv KX	Bookman-DemiItalic	$FP/b018035l.afm
afmconv NR	NewCenturySchlbk-Roman	$FP/c059013l.afm
afmconv NI	NewCenturySchlbk-Italic	$FP/c059033l.afm
afmconv NB	NewCenturySchlbk-Bold	$FP/c059016l.afm
afmconv NX	NewCenturySchlbk-BoldItalic	$FP/c059036l.afm
afmconv PA	Palatino-Roman		$FP/p052003l.afm
afmconv PR	Palatino-Roman		$FP/p052003l.afm
afmconv PI	Palatino-Italic		$FP/p052023l.afm
afmconv PB	Palatino-Bold		$FP/p052004l.afm
afmconv PX	Palatino-BoldItalic	$FP/p052024l.afm
afmconv C	Courier			$FP/n022003l.afm
afmconv CO	Courier			$FP/n022003l.afm
afmconv CW	Courier			$FP/n022003l.afm
afmconv CI	Courier-Oblique		$FP/n022023l.afm
afmconv CB	Courier-Bold		$FP/n022004l.afm
afmconv CX	Courier-BoldOblique	$FP/n022024l.afm
afmconv ZI	ZapfChancery-MediumItalic	$FP/z003034l.afm
afmconv ZD	ZapfDingbats		$FP/z003034l.afm

# For otf and ttf files, we assume the postscript name of the font
# can be obtained by dropping its extension.  Otherwise, remove the
# -p argument of mktrfn in otfconv function.

for f in $FP/*.otf $FP/*.ttf
do
	FN="`basename $f .otf`"
	FN="`basename $FN .ttf`"
	echo $FN
	otfconv $FN $f
done
