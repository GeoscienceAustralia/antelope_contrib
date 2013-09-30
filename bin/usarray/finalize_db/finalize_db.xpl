#
#  add -p option
#  force user to not use default pf
#  convert to run_cmd
#  add -v option

use strict ; 
use Datascope ; 
use archive ;
use utilfunct ;
use Getopt::Std ;

our ( $opt_d, $opt_n, $opt_p, $opt_t );
#our ( $dbpath, $dblocks, $dbidserver) ;

{    
    my ( $Pf, $base, $cmd, $db, $dir, $problems, $suffix) ;
    my ( %pf );
    my $pgm = $0 ; 
    $pgm =~ s".*/"" ;
    elog_init($pgm, @ARGV) ;
    elog_notify("\n\n$0 @ARGV") ;
#
#  get arguments
#
    if ( ! &getopts('ndtp:') || @ARGV == 0 ) { 
        die ( "\nUsage: $0  [-d] [-n] [-t] [-p pf] db1 [ db2 db3 ... ]\n\n" ) ; 
    }

    $Pf = $opt_p || $pgm ;
    
    %pf = getparam($Pf, 1, 0);
    
    if ( $pf{dbpath} =~ /none/ ) {
        elog_die ( "\n\n\n	Default pf file used!\n\n	Create your own $pgm pf file in \$PFPATH!\n\n" ) ;
    }
    
    foreach $db (@ARGV) {
        ($dir,$base,$suffix) = parsepath($db) ;
        elog_notify("\n\nprocessing $db \n");
        
        &cssdescriptor ($db,$pf{dbpath},$pf{dblocks},$pf{dbidserver}) unless ( $opt_t || $opt_n ) ;

        $problems = &dbtidy($db,$problems) unless $opt_d ;
        
        $cmd  = "dbverify -tcijk -P wfdisc,site,wfsrb,deployment,dmcfiles,comm,q330comm,staq330,stabaler,dlcalwf,dlsite " ;
        $cmd .= "-A instrument,sitechan -X dbverify_eventdbs $db > $dir/verify_$base  2>&1 ";
        elog_notify($cmd);
        system($cmd) unless $opt_n;
    }
    exit;
}

sub dbtidy { # $problems = &dbtidy($db,$problems);
    my ( $db, $problems ) = @_;
    my ( $chan, $cmd, $etype, $idserver, $lddate, $snr );
    my ( @db );

    $cmd = "dbcrunch $db";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsubset $db.arrival \"iphase =~ /del/\" | dbdelete -v -";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "fix $db.arrival snr < 0";
    elog_notify($cmd);
    @db = dbopen($db,'r+');
    @db = dblookup(@db,0,"arrival",0,0);
    for ($db[3] = 0; $db[3] < dbquery(@db,'dbRECORD_COUNT'); $db[3]++) {
        ( $snr, $chan, $lddate ) = dbgetv(@db,qw( snr chan lddate ));
        if ( $snr < 0 && ! $opt_n ) {
           dbputv(@db,"snr",-1,"lddate",$lddate);
        }
    }
    @db = dblookup(@db,0,"origin",0,0);
    for ($db[3] = 0; $db[3] < dbquery(@db,'dbRECORD_COUNT'); $db[3]++) {
        ( $etype, $lddate ) = dbgetv(@db,qw( etype lddate ));
        if ( $etype =~ /^\s+$/  && ! $opt_n ) {
           dbputv(@db,"etype","-","lddate",$lddate);
        }
    }
    dbclose(@db);

    $cmd = "dbset -l $db.assoc timeres nan NULL";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbset -l $db.assoc timeres \"-nan\" NULL";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbset -l $db.assoc timeres NaN NULL";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbset -l $db.assoc timeres \"-NaN\" NULL";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.origin time ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.arrival time ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    @db = dbopen($db,'r');
    $idserver = dbquery(@db,'dbIDSERVER');
    dbclose(@db);

    if ($idserver) {
        $cmd = "dbfixids -x inid,chanid -i arid,orid -s $idserver $db";
        elog_notify($cmd);
        system($cmd) unless $opt_n;
    } else {
        $cmd = "dbfixids $db orid ";
        elog_notify($cmd);
        system($cmd) unless $opt_n;

        $cmd = "dbfixids $db arid ";
        elog_notify($cmd);
        system($cmd) unless $opt_n;
    }

    $cmd = "dbsort -o $db.event prefor ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    if ($idserver) {
        $cmd = "dbfixids -x inid,chanid -i evid -s $idserver $db";
        elog_notify($cmd);
        system($cmd) unless $opt_n;
    } else {
        $cmd = "dbfixids $db evid ";
        elog_notify($cmd);
        system($cmd) unless $opt_n;
    }

    $cmd = "dbsort -o $db.origerr orid ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.netmag orid ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.emodel orid ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.stamag orid sta";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    if ($idserver) {
        $cmd = "dbfixids -x inid,chanid -i magid -s $idserver $db";
        elog_notify($cmd);
        system($cmd) unless $opt_n;
    } else {
        $cmd = "dbfixids $db magid ";
        elog_notify($cmd);
        system($cmd) unless $opt_n;
    }

    $cmd = "dbfixchanids $db  > /dev/null 2>&1 ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;
    
    $cmd = "dbjoin $db.wfdisc calibration | dbset -v - wfdisc.calib '*' calibration.calib ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbjoin $db.wfdisc calibration | dbset -v - wfdisc.calper '*' calibration.calper ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbjoin $db.wfdisc calibration | dbset -v - wfdisc.segtype '*' calibration.segtype ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.assoc orid arid ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.predarr orid arid ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    $cmd = "dbsort -o $db.wfdisc time sta chan ";
    elog_notify($cmd);
    system($cmd) unless $opt_n;

    return ($problems);
}


