#include <mutex>
#include <condition_variable>
#include <deque>
#include "DiceJob.h" 

unordered_map<string, cmd> mCommand = {
	{"syscheck",check_system},
	{"autosave",auto_save},
	{"clrimage",clear_image},
	{"reload",cq_restart},
	{"die",cq_exit},
	{"heartbeat",cloud_beat},
	{"update",dice_update}
};


void DiceJob::exec() {
	if (auto it = mCommand.find(cmd_key); it != mCommand.end()) {
		it->second(*this);
	}
	else return;
}
void DiceJob::echo(const std::string& msg) {
	switch (fromChat.second) {
	case CQ::msgtype::Private:
		CQ::sendPrivateMsg(fromQQ, msg);
		break;
	case CQ::msgtype::Group:
		CQ::sendGroupMsg(fromChat.first, msg);
		break;
	case CQ::msgtype::Discuss:
		CQ::sendDiscussMsg(fromChat.first, msg);
		break;
	}
}
void DiceJob::note(const std::string& strMsg, int note_lv = 0b1) {
	ofstream fout(string("DiceData\\audit\\log") + to_string(console.DiceMaid) + "_" + printDate() + ".txt", ios::out | ios::app);
	fout << printSTNow() << "\t" << note_lv << "\t" << printLine(strMsg) << std::endl;
	fout.close();
	echo(strMsg);
	string note = fromQQ ? getName(fromQQ) + strMsg : strMsg;
	for (const auto& [ct, level] : console.NoticeList) {
		if (!(level & note_lv) || pair(fromQQ, CQ::msgtype::Private) == ct || ct == fromChat)continue;
		AddMsgToQueue(note, ct);
	}
}

// 待处理任务队列
std::queue<DiceJobDetail> queueJob;
std::mutex mtQueueJob;
std::condition_variable cvJob;
std::condition_variable cvJobWaited;
//延时任务队列
using waited_job = pair<time_t, DiceJobDetail>;
std::priority_queue<waited_job, std::deque<waited_job>> queueJobWaited;
std::mutex mtJobWaited;

void jobHandle() {
	while (Enabled) {
		//监听作业队列
		if (std::unique_lock<std::mutex> lock_queue(mtQueueJob); !queueJob.empty()) {
			DiceJob job(queueJob.front());
			queueJob.pop();
			lock_queue.unlock();
			job.exec();
			cvJobWaited.notify_one();
		}
		else{
			cvJob.wait(lock_queue, []() {return !queueJob.empty(); });
		}
	}
}
void jobWait() {
	while (Enabled) {
		//检查定时作业
		if (std::unique_lock<std::mutex> lock_queue(mtJobWaited); !queueJobWaited.empty() && queueJobWaited.top().first <= time(NULL)) {
			sch.push_job(&queueJobWaited.top().second);
			queueJobWaited.pop();
		}
		else {
			cvJobWaited.wait_for(lock_queue, 1s);
		}
	}
}
//将任务加入执行队列
void DiceScheduler::push_job(const DiceJobDetail* job) {
	if (!Enabled)return;
	std::unique_lock<std::mutex> lock_queue(mtQueueJob);
	queueJob.push(*job);
	lock_queue.unlock();
	cvJob.notify_one();
}
//将任务加入等待队列
void DiceScheduler::add_job_for(unsigned int waited, const DiceJobDetail* job) {
	if (!Enabled)return;
	std::unique_lock<std::mutex> lock_queue(mtJobWaited);
	queueJobWaited.emplace(time(NULL) + waited, *job);
	lock_queue.unlock();
	cvJob.notify_one();
}
void DiceScheduler::add_job_until(time_t cloc, const DiceJobDetail* job) {
	if (!Enabled)return;
	std::unique_lock<std::mutex> lock_queue(mtJobWaited);
	queueJobWaited.emplace(cloc, *job);
	lock_queue.unlock();
	cvJob.notify_one();
}
std::unique_ptr<std::thread> threadJobs;

bool DiceScheduler::is_job_cold(const char* cmd) {
	return untilJobs[cmd] > time(NULL);
}
void DiceScheduler::refresh_cold(const char* cmd, time_t until) {
	untilJobs[cmd] = until;
}

void DiceScheduler::start() {
	threadJobs = std::make_unique<std::thread>(jobHandle);
	threadJobs->detach();
	std::thread thWaited(jobWait);
	thWaited.detach();
	push_job(&DiceJobDetail("heartbeat"));
	push_job(&DiceJobDetail("syscheck"));
	if (console["AutoSaveInterval"] > 0)add_job_for(console["AutoSaveInterval"] * 60, &DiceJobDetail("autosave"));
	if (console["AutoClearImage"] > 0)add_job_for(console["AutoClearImage"] * 60 * 60, &DiceJobDetail("clrimage"));
	else add_job_for(60 * 60, &DiceJobDetail("clrimage"));
}
void DiceScheduler::end() {
	threadJobs.reset();
}


