gethr <- function(file="/scr/tmp/maclean/ttadjust_nov20_hr.txt")
{
    require("eolts")
    options(time.zone="UTC")
    x <- scan(file=file,what=list(y=1,m=1,d=1,hms="",
            toff= 1, tdiff=1, tdiffuncorr=1, tdiffmin=1, ndt=1))
    tx <- utime(paste(x$y,x$m,x$d,x$hms))

    xt <- nts(matrix(c(x$tdiff, x$tdiffuncorr, x$tdiffmin,x$ndt),ncol=4,
            dimnames=list(NULL, c("tdiff", "tdiffuncorr", "tdiffmin", "I"))),
                tx,units=c(rep("sec",3),""))
    xt
}

plot_ttadj <- function(xhr, title="WCR-TEST", var="PSFD", ...) 
{
    require("eolts")

    par(mfrow=c(3,1), mgp=c(2,0.5,0))

    # times in file are the raw data times. The adjusted times
    # are traw - tdiff
    dtraw <- diff(positions(xhr))
    dtadj <- diff(positions(xhr) - xhr@data[,"tdiff"])

    nr <- nrow(xhr)
    subt <- paste0(
	format(positions(xhr[1,]),format="%Y %m %d %H:%M:%S",time.zone="UTC")," - ",
	format(positions(xhr[nr,]),format="%H:%M:%S",time.zone="UTC"))

    par(mar=c(2,3,3,1))
    plot(0:(nr-1), xhr[,"tdiffuncorr"], xlab="I", ylab="tdiff (sec)",
	main=paste(title, var, subt))
    par(mar=c(2,3,2,1))
    ylim <- range(c(dtraw,dtadj))
    plot(0:(nr-1), c(NA,dtraw), xlab="I", ylab="dtraw (sec)", ylim=ylim)
    par(mar=c(3,3,2,1))
    if (var == "PSFD") {
        plot(0:(nr-1), c(NA,dtadj), xlab="I", ylab="dtadj (sec)", ylim=ylim)
    } else {
        plot(0:(nr-1), c(NA,dtadj), xlab="I", ylab="dtadj (sec)")
    }

    par(mfrow=c(2,1), mgp=c(2,1,0), mar=c(3,3,3,1))

    hist(dtraw, xlim=ylim, breaks=100, main=paste0(var, ", dt raw, ", subt), xlab="sec")
    if (var == "PSFD") {
        # use same xlimits for PSFD raw and adjusted
	hist(dtadj, xlim=ylim, breaks=100, main=paste0(var, ", dt adj, ", subt), xlab="sec")
    } else {
        # limits on QCF raw are huge, let adjusted have own scale
	hist(dtadj, breaks=100, main=paste0(var, ", dt adj, ", subt), xlab="sec")
    }
}

