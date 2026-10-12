from pathlib import Path
import importlib.util,json,tempfile,unittest
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('temporal',root/'scripts/analyze_temporal_capture.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class Analysis(unittest.TestCase):
    def test_candidates_are_not_reported_as_motion_vectors(self):
        header=dict(kind='gh2-temporal-input-candidates',schema=1,complete=False)
        draw=dict(vs=1,ps=2,depth_info=3,depth_control=6,surface_info=1280,
                  depth_resource=[1280,720,39,1],vs_constant_base=0,constants=[[4,1,2,3,4]])
        draw2=dict(draw,constants=[[4,1,2,3,5]])
        frame=dict(frame=0,input=[1280,720],seen_draws=3,draws=[draw,draw2])
        with tempfile.TemporaryDirectory(dir=root/'build') as directory:
            file=Path(directory)/'capture.jsonl'
            file.write_text(json.dumps(header)+'\n'+json.dumps(frame)+'\n')
            report=module.analyze(file)
        self.assertFalse(report['validated_dlss_inputs']);self.assertFalse(report['capture_complete'])
        self.assertEqual(report['truncated_frames'],1)
        self.assertEqual(report['varying_vertex_constants'],{'0000000000000001':[4]})
        self.assertEqual(report['candidate_passes'][0]['draws'],2)
        self.assertTrue(any('different objects' in text for text in report['limitations']))

    def test_unknown_schema_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=root/'build') as directory:
            file=Path(directory)/'capture.jsonl';file.write_text('{"schema":2}\n')
            with self.assertRaises(ValueError):module.analyze(file)

if __name__=='__main__':unittest.main()
